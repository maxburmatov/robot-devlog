#include "MicrophoneSensor.h"

#include <limits.h>
#include <hal/adc_types.h>
#include <soc/soc_caps.h>

static_assert(config::MIC_CLAP_HISTORY_SIZE > 0,
              "Microphone clap history cannot be empty");
static_assert(config::MIC_SOUND_HISTORY_SIZE > 0,
              "Microphone sound history cannot be empty");
static_assert(config::MIC_SOUND_REQUIRED_ACTIVE <=
                  config::MIC_SOUND_HISTORY_SIZE,
              "Required active windows must fit sound history");
static_assert(config::MIC_SAMPLE_INTERVAL_MS > 0,
              "Microphone sample interval must be positive");
static_assert(config::MIC_WINDOW_MS % config::MIC_SAMPLE_INTERVAL_MS == 0,
              "Analysis window must contain a whole number of samples");
static_assert(config::MIC_CLAP_DIAGNOSTIC_WINDOW_MS %
                      config::MIC_SAMPLE_INTERVAL_MS ==
                  0,
              "Diagnostic window must contain a whole number of samples");
static_assert(config::MIC_AUDIO_SAMPLE_RATE %
                      (1000UL / config::MIC_SAMPLE_INTERVAL_MS) ==
                  0,
              "Audio rate must downsample exactly to the legacy classifier");

namespace {

constexpr UBaseType_t ANALYSIS_QUEUE_LENGTH = 8;
constexpr UBaseType_t DIAGNOSTIC_QUEUE_LENGTH = 32;

}  // namespace

bool MicrophoneSensor::begin(MusicAnalyzer& musicAnalyzer) {
  pinMode(config::MIC_PIN, INPUT);
  musicAnalyzer_ = &musicAnalyzer;

  resetClassifier();
  analysisQueue_ = xQueueCreate(ANALYSIS_QUEUE_LENGTH, sizeof(SampleWindow));
  if (config::MIC_DEBUG) {
    diagnosticQueue_ =
        xQueueCreate(DIAGNOSTIC_QUEUE_LENGTH, sizeof(SampleWindow));
  }
  if (analysisQueue_ == nullptr ||
      (config::MIC_DEBUG && diagnosticQueue_ == nullptr)) {
    if (analysisQueue_ != nullptr) {
      vQueueDelete(analysisQueue_);
      analysisQueue_ = nullptr;
    }
    if (diagnosticQueue_ != nullptr) {
      vQueueDelete(diagnosticQueue_);
      diagnosticQueue_ = nullptr;
    }
    return false;
  }

  adc_unit_t unit = ADC_UNIT_1;
  adc_channel_t channel = ADC_CHANNEL_0;
  if (adc_continuous_io_to_channel(config::MIC_PIN, &unit, &channel) != ESP_OK ||
      unit != ADC_UNIT_1) {
    return false;
  }
  const adc_continuous_handle_cfg_t handleConfig = {
      .max_store_buf_size = config::MUSIC_FFT_SIZE * SOC_ADC_DIGI_RESULT_BYTES * 4,
      .conv_frame_size = config::MUSIC_FFT_SIZE * SOC_ADC_DIGI_RESULT_BYTES,
  };
  if (adc_continuous_new_handle(&handleConfig, &adcHandle_) != ESP_OK) {
    return false;
  }
  adc_digi_pattern_config_t pattern = {};
  pattern.atten = ADC_ATTEN_DB_12;
  pattern.channel = channel;
  pattern.unit = ADC_UNIT_1;
  pattern.bit_width = ADC_BITWIDTH_12;
  const adc_continuous_config_t adcConfig = {
      .pattern_num = 1,
      .adc_pattern = &pattern,
      .sample_freq_hz = config::MIC_AUDIO_SAMPLE_RATE,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
      .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };
  if (adc_continuous_config(adcHandle_, &adcConfig) != ESP_OK ||
      adc_continuous_start(adcHandle_) != ESP_OK) {
    adc_continuous_deinit(adcHandle_);
    adcHandle_ = nullptr;
    return false;
  }

  const BaseType_t taskCreated = xTaskCreate(
      samplingTaskEntry, "deskbot-mic", config::MIC_TASK_STACK_WORDS, this,
      config::MIC_TASK_PRIORITY, &samplingTaskHandle_);
  if (taskCreated != pdPASS) {
    vQueueDelete(analysisQueue_);
    analysisQueue_ = nullptr;
    if (diagnosticQueue_ != nullptr) {
      vQueueDelete(diagnosticQueue_);
      diagnosticQueue_ = nullptr;
    }
    samplingTaskHandle_ = nullptr;
    adc_continuous_stop(adcHandle_);
    adc_continuous_deinit(adcHandle_);
    adcHandle_ = nullptr;
    return false;
  }

  initialized_ = true;
  return true;
}

MicrophoneEvent MicrophoneSensor::update(unsigned long now) {
  (void)now;
  if (!initialized_) {
    return MicrophoneEvent::None;
  }

  processDiagnosticWindows();

  uint32_t generation = 0;
  portENTER_CRITICAL(&samplingLock_);
  generation = samplingGeneration_;
  portEXIT_CRITICAL(&samplingLock_);

  MicrophoneEvent event = MicrophoneEvent::None;
  SampleWindow window;
  while (xQueueReceive(analysisQueue_, &window, 0) == pdTRUE) {
    if (suppressed_ || window.generation != generation) {
      continue;
    }
    const MicrophoneEvent windowEvent = finishWindow(window);
    if (event == MicrophoneEvent::None) {
      event = windowEvent;
    }
  }
  return event;
}

void MicrophoneSensor::setSuppressed(bool suppressed) {
  if (suppressed_ == suppressed) {
    return;
  }

  suppressed_ = suppressed;
  portENTER_CRITICAL(&samplingLock_);
  ++samplingGeneration_;
  portEXIT_CRITICAL(&samplingLock_);
  resetClassifier();
  previousDiagnosticAmplitude_ = 0;
  previousDiagnosticEndedAt_ = 0;
  diagnosticTailWindows_ = 0;
  if (musicAnalyzer_ != nullptr) {
    musicAnalyzer_->setSuppressed(suppressed_, samplingGeneration_);
  }
}

int MicrophoneSensor::amplitude() const { return amplitude_; }

int MicrophoneSensor::rise() const { return rise_; }

int MicrophoneSensor::preAverage() const { return preAverage_; }

void MicrophoneSensor::samplingTaskEntry(void* context) {
  static_cast<MicrophoneSensor*>(context)->samplingTask();
}

void MicrophoneSensor::samplingTask() {
  const uint16_t analysisSamples = static_cast<uint16_t>(
      config::MIC_WINDOW_MS / config::MIC_SAMPLE_INTERVAL_MS);
  const uint16_t diagnosticSamples = static_cast<uint16_t>(
      config::MIC_CLAP_DIAGNOSTIC_WINDOW_MS /
      config::MIC_SAMPLE_INTERVAL_MS);

  SampleWindow analysis = {4095, 0, 0, 0, 0};
  SampleWindow diagnostic = {4095, 0, 0, 0, 0};
  size_t audioFrameIndex = 0;
  uint8_t decimation = 0;
  uint32_t activeGeneration = 0;
  static_assert(DMA_BUFFER_BYTES ==
                    config::MUSIC_FFT_SIZE * SOC_ADC_DIGI_RESULT_BYTES,
                "DMA buffer must hold one complete ADC frame");

  for (;;) {
    uint32_t bytesRead = 0;
    const esp_err_t readResult = adc_continuous_read(
        adcHandle_, dmaBuffer_, sizeof(dmaBuffer_), &bytesRead, 100);
    if (readResult != ESP_OK) {
      continue;
    }

    uint32_t generation = 0;
    portENTER_CRITICAL(&samplingLock_);
    generation = samplingGeneration_;
    portEXIT_CRITICAL(&samplingLock_);
    if (generation != activeGeneration) {
      activeGeneration = generation;
      analysis = {4095, 0, 0, 0, generation};
      diagnostic = {4095, 0, 0, 0, generation};
      audioFrameIndex = 0;
      decimation = 0;
    }

    for (uint32_t offset = 0;
         offset + SOC_ADC_DIGI_RESULT_BYTES <= bytesRead;
         offset += SOC_ADC_DIGI_RESULT_BYTES) {
      const adc_digi_output_data_t* result =
          reinterpret_cast<const adc_digi_output_data_t*>(dmaBuffer_ + offset);
      const uint16_t sample = static_cast<uint16_t>(result->type2.data);
      audioFrame_[audioFrameIndex++] = sample;
      if (audioFrameIndex == config::MUSIC_FFT_SIZE) {
        if (musicAnalyzer_ != nullptr) {
          musicAnalyzer_->submitFrame(audioFrame_, millis(), generation);
        }
        audioFrameIndex = 0;
      }

      if (++decimation < config::MIC_AUDIO_SAMPLE_RATE /
                              (1000UL / config::MIC_SAMPLE_INTERVAL_MS)) {
        continue;
      }
      decimation = 0;
      analysis.minimum = min(analysis.minimum, static_cast<int>(sample));
      analysis.maximum = max(analysis.maximum, static_cast<int>(sample));
      ++analysis.sampleCount;
      diagnostic.minimum = min(diagnostic.minimum, static_cast<int>(sample));
      diagnostic.maximum = max(diagnostic.maximum, static_cast<int>(sample));
      ++diagnostic.sampleCount;

      if (diagnostic.sampleCount >= diagnosticSamples) {
        diagnostic.endedAt = millis();
        diagnostic.generation = generation;
        if (diagnosticQueue_ != nullptr) {
          xQueueSend(diagnosticQueue_, &diagnostic, 0);
        }
        diagnostic = {4095, 0, 0, 0, generation};
      }
      if (analysis.sampleCount >= analysisSamples) {
        analysis.endedAt = millis();
        analysis.generation = generation;
        xQueueSend(analysisQueue_, &analysis, 0);
        analysis = {4095, 0, 0, 0, generation};
      }
    }
  }
}

void MicrophoneSensor::processDiagnosticWindows() {
  if (!config::MIC_DEBUG || diagnosticQueue_ == nullptr) {
    return;
  }

  uint32_t generation = 0;
  portENTER_CRITICAL(&samplingLock_);
  generation = samplingGeneration_;
  portEXIT_CRITICAL(&samplingLock_);

  SampleWindow window;
  while (xQueueReceive(diagnosticQueue_, &window, 0) == pdTRUE) {
    if (suppressed_ || window.generation != generation) {
      continue;
    }

    const int diagnosticAmplitude = window.maximum - window.minimum;
    const int diagnosticRise =
        diagnosticAmplitude - previousDiagnosticAmplitude_;
    const unsigned long diagnosticDuration =
        previousDiagnosticEndedAt_ == 0
            ? 0
            : window.endedAt - previousDiagnosticEndedAt_;
    const bool active =
        diagnosticAmplitude >= config::MIC_DIAGNOSTIC_LOG_THRESHOLD;
    if (active) {
      diagnosticTailWindows_ = config::MIC_DIAGNOSTIC_TAIL_WINDOWS;
    }

    if (active || diagnosticTailWindows_ > 0) {
      Serial.printf("MIC10 AMP=%d RISE=%d N=%u DT=%lums\n",
                    diagnosticAmplitude, diagnosticRise, window.sampleCount,
                    diagnosticDuration);
      if (!active) {
        --diagnosticTailWindows_;
      }
    }
    previousDiagnosticAmplitude_ = diagnosticAmplitude;
    previousDiagnosticEndedAt_ = window.endedAt;
  }
}

MicrophoneEvent MicrophoneSensor::finishWindow(const SampleWindow& window) {
  const unsigned long now = window.endedAt;
  amplitude_ = window.maximum - window.minimum;
  rise_ = amplitude_ - previousAmplitude_;

  long preTotal = 0;
  for (uint8_t i = 0; i < amplitudeHistoryCount_; ++i) {
    preTotal += amplitudeHistory_[i];
  }
  preAverage_ = amplitudeHistoryCount_ == 0
                    ? 0
                    : static_cast<int>(preTotal / amplitudeHistoryCount_);

  if (config::MIC_DEBUG) {
    Serial.printf("MIC AMP=%d RISE=%d PRE=%d\n", amplitude_, rise_,
                  preAverage_);
  }

  const bool soundActive = amplitude_ >= config::MIC_SOUND_THRESHOLD;
  pushSoundActivity(soundActive);

  if (suppressed_) {
    resetClassifier();
    previousAmplitude_ = amplitude_;
    return MicrophoneEvent::None;
  }

  if (clapCandidate_) {
    const unsigned long duration = now - clapCandidateStartedAt_;
    if (duration > config::MIC_CLAP_MAX_DURATION_MS) {
      clapCandidate_ = false;
      if (config::MIC_DEBUG) {
        Serial.printf("MIC CANDIDATE: NOT CLAP duration=%lums\n", duration);
      }
    } else if (amplitude_ <= config::MIC_CLAP_END_THRESHOLD) {
      clapCandidate_ = false;
      if (!cooldownActive(now)) {
        for (uint8_t i = 0; i < config::MIC_SOUND_HISTORY_SIZE; ++i) {
          soundHistory_[i] = false;
        }
        soundHistoryCount_ = 0;
        soundHistoryIndex_ = 0;
        hasEmittedEvent_ = true;
        lastEventAt_ = now;
        pushAmplitude(amplitude_);
        previousAmplitude_ = amplitude_;
        if (config::MIC_DEBUG) {
          Serial.printf("MIC CLAP confirmed duration=%lums\n", duration);
        }
        return MicrophoneEvent::Clap;
      }
    }
  } else if (!cooldownActive(now) &&
             amplitudeHistoryCount_ == config::MIC_CLAP_HISTORY_SIZE &&
             amplitude_ >= config::MIC_CLAP_PEAK_THRESHOLD &&
             rise_ >= config::MIC_CLAP_MIN_RISE &&
             preAverage_ <= config::MIC_CLAP_PRE_AVG_MAX) {
    clapCandidate_ = true;
    clapCandidateStartedAt_ = now;
    if (config::MIC_DEBUG) {
      Serial.println("MIC CANDIDATE: CLAP");
    }
  }

  MicrophoneEvent event = MicrophoneEvent::None;
  if (!clapCandidate_ && !cooldownActive(now) &&
      soundHistoryCount_ == config::MIC_SOUND_HISTORY_SIZE) {
    uint8_t activeCount = 0;
    for (uint8_t i = 0; i < config::MIC_SOUND_HISTORY_SIZE; ++i) {
      activeCount += soundHistory_[i] ? 1 : 0;
    }
    if (activeCount >= config::MIC_SOUND_REQUIRED_ACTIVE) {
      for (uint8_t i = 0; i < config::MIC_SOUND_HISTORY_SIZE; ++i) {
        soundHistory_[i] = false;
      }
      soundHistoryCount_ = 0;
      soundHistoryIndex_ = 0;
      hasEmittedEvent_ = true;
      lastEventAt_ = now;
      event = MicrophoneEvent::Sound;
    }
  }

  pushAmplitude(amplitude_);
  previousAmplitude_ = amplitude_;
  return event;
}

void MicrophoneSensor::pushAmplitude(int value) {
  amplitudeHistory_[amplitudeHistoryIndex_] = value;
  amplitudeHistoryIndex_ =
      (amplitudeHistoryIndex_ + 1) % config::MIC_CLAP_HISTORY_SIZE;
  if (amplitudeHistoryCount_ < config::MIC_CLAP_HISTORY_SIZE) {
    ++amplitudeHistoryCount_;
  }
}

void MicrophoneSensor::pushSoundActivity(bool active) {
  soundHistory_[soundHistoryIndex_] = active;
  soundHistoryIndex_ =
      (soundHistoryIndex_ + 1) % config::MIC_SOUND_HISTORY_SIZE;
  if (soundHistoryCount_ < config::MIC_SOUND_HISTORY_SIZE) {
    ++soundHistoryCount_;
  }
}

void MicrophoneSensor::resetClassifier() {
  for (uint8_t i = 0; i < config::MIC_CLAP_HISTORY_SIZE; ++i) {
    amplitudeHistory_[i] = 0;
  }
  for (uint8_t i = 0; i < config::MIC_SOUND_HISTORY_SIZE; ++i) {
    soundHistory_[i] = false;
  }
  amplitudeHistoryIndex_ = 0;
  amplitudeHistoryCount_ = 0;
  soundHistoryIndex_ = 0;
  soundHistoryCount_ = 0;
  clapCandidate_ = false;
  clapCandidateStartedAt_ = 0;
  previousAmplitude_ = 0;
  rise_ = 0;
  preAverage_ = 0;
}

bool MicrophoneSensor::cooldownActive(unsigned long now) const {
  return hasEmittedEvent_ &&
         now - lastEventAt_ < config::MIC_EVENT_COOLDOWN_MS;
}
