#include "AudioCapture.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cstring>

static_assert(config::AUDIO_CAPTURE_RING_SAMPLES >=
                  config::AUDIO_INPUT_SAMPLE_RATE * 2,
              "Audio ring must retain at least two seconds");

bool AudioCapture::begin() {
  if (!config::AUDIO_CAPTURE_ENABLED) {
    Serial.println("AUDIO: input disabled in Config.h");
    return false;
  }

  ring_ = static_cast<int16_t*>(heap_caps_malloc(
      config::AUDIO_CAPTURE_RING_SAMPLES * sizeof(int16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (ring_ == nullptr) {
    Serial.println("AUDIO: PSRAM ring allocation failed");
    return false;
  }
  memset(ring_, 0, config::AUDIO_CAPTURE_RING_SAMPLES * sizeof(int16_t));

  if (config::SOUND_DETECTOR_ENABLED) {
    soundQueue_ = xQueueCreate(config::AUDIO_CAPTURE_QUEUE_FRAMES,
                               sizeof(AudioFrame));
  }
  if (config::MUSIC_ANALYZER_ENABLED) {
    musicQueue_ = xQueueCreate(config::AUDIO_CAPTURE_QUEUE_FRAMES,
                               sizeof(AudioFrame));
  }
  if ((config::SOUND_DETECTOR_ENABLED && soundQueue_ == nullptr) ||
      (config::MUSIC_ANALYZER_ENABLED && musicQueue_ == nullptr)) {
    Serial.println("AUDIO: consumer queue allocation failed");
    return false;
  }

  const i2s_config_t i2sConfig = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = config::AUDIO_INPUT_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = config::AUDIO_CAPTURE_FRAME_SAMPLES,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };
  const i2s_pin_config_t pins = {
      .bck_io_num = config::INMP441_BCLK_PIN,
      .ws_io_num = config::INMP441_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = config::INMP441_DATA_PIN,
  };

  esp_err_t result = i2s_driver_install(I2S_NUM_1, &i2sConfig, 0, nullptr);
  if (result == ESP_OK) result = i2s_set_pin(I2S_NUM_1, &pins);
  if (result != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_1);
    Serial.printf("AUDIO: INMP441 I2S1 initialization failed (%d)\n", result);
    return false;
  }

  if (xTaskCreatePinnedToCore(taskEntry, "deskbot-capture",
                              config::AUDIO_CAPTURE_TASK_STACK_WORDS, this,
                              config::AUDIO_CAPTURE_TASK_PRIORITY,
                              &taskHandle_,
                              config::AUDIO_CAPTURE_TASK_CORE) != pdPASS) {
    i2s_driver_uninstall(I2S_NUM_1);
    Serial.println("AUDIO: capture task creation failed");
    return false;
  }

  ready_ = true;
  Serial.println("AUDIO: INMP441 I2S1 initialized");
  Serial.printf("AUDIO: PCM16 %lu Hz\n",
                static_cast<unsigned long>(config::AUDIO_INPUT_SAMPLE_RATE));
  return true;
}

bool AudioCapture::ready() const { return ready_; }

bool AudioCapture::receiveSoundFrame(AudioFrame& frame, TickType_t waitTicks) {
  return soundQueue_ != nullptr &&
         xQueueReceive(soundQueue_, &frame, waitTicks) == pdTRUE;
}

bool AudioCapture::receiveMusicFrame(AudioFrame& frame, TickType_t waitTicks) {
  return musicQueue_ != nullptr &&
         xQueueReceive(musicQueue_, &frame, waitTicks) == pdTRUE;
}

bool AudioCapture::copyLatestWindow(int16_t* destination, size_t sampleCount,
                                    uint64_t& previousEndSample,
                                    size_t stepSamples) {
  if (!ready_ || destination == nullptr || sampleCount == 0 ||
      sampleCount > config::AUDIO_CAPTURE_RING_SAMPLES) {
    return false;
  }

  uint64_t endSample;
  portENTER_CRITICAL(&lock_);
  endSample = totalSamples_;
  portEXIT_CRITICAL(&lock_);
  if (endSample < sampleCount ||
      (previousEndSample != 0 && endSample - previousEndSample < stepSamples)) {
    return false;
  }

  const uint64_t startSample = endSample - sampleCount;
  for (size_t i = 0; i < sampleCount; ++i) {
    destination[i] = ring_[(startSample + i) %
                           config::AUDIO_CAPTURE_RING_SAMPLES];
  }

  uint64_t afterCopy;
  portENTER_CRITICAL(&lock_);
  afterCopy = totalSamples_;
  portEXIT_CRITICAL(&lock_);
  if (afterCopy - startSample > config::AUDIO_CAPTURE_RING_SAMPLES) {
    return false;
  }
  previousEndSample = endSample;
  return true;
}

bool AudioCapture::copyWindowEndingAt(int16_t* destination,
                                      size_t sampleCount,
                                      uint64_t endSample) {
  if (!ready_ || destination == nullptr || sampleCount == 0 ||
      sampleCount > config::AUDIO_CAPTURE_RING_SAMPLES ||
      endSample < sampleCount) {
    return false;
  }

  uint64_t availableEnd;
  portENTER_CRITICAL(&lock_);
  availableEnd = totalSamples_;
  portEXIT_CRITICAL(&lock_);
  const uint64_t startSample = endSample - sampleCount;
  if (availableEnd < endSample ||
      availableEnd - startSample > config::AUDIO_CAPTURE_RING_SAMPLES) {
    return false;
  }

  for (size_t i = 0; i < sampleCount; ++i) {
    destination[i] =
        ring_[(startSample + i) % config::AUDIO_CAPTURE_RING_SAMPLES];
  }

  portENTER_CRITICAL(&lock_);
  availableEnd = totalSamples_;
  portEXIT_CRITICAL(&lock_);
  return availableEnd - startSample <= config::AUDIO_CAPTURE_RING_SAMPLES;
}

AudioCaptureStats AudioCapture::stats() const {
  portENTER_CRITICAL(&lock_);
  const AudioCaptureStats result = stats_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

uint64_t AudioCapture::totalSamples() const {
  portENTER_CRITICAL(&lock_);
  const uint64_t result = totalSamples_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

void AudioCapture::taskEntry(void* context) {
  static_cast<AudioCapture*>(context)->task();
}

void AudioCapture::task() {
  int32_t raw[config::AUDIO_CAPTURE_FRAME_SAMPLES] = {};
  AudioFrame frame;
  size_t accumulatedSamples = 0;
  for (;;) {
    size_t bytesRead = 0;
    const size_t remainingBytes =
        (config::AUDIO_CAPTURE_FRAME_SAMPLES - accumulatedSamples) *
        sizeof(raw[0]);
    const esp_err_t result =
        i2s_read(I2S_NUM_1, raw + accumulatedSamples, remainingBytes,
                 &bytesRead, pdMS_TO_TICKS(100));
    if (result != ESP_OK || bytesRead == 0) {
      portENTER_CRITICAL(&lock_);
      ++stats_.readErrors;
      portEXIT_CRITICAL(&lock_);
      continue;
    }

    const size_t samplesRead = bytesRead / sizeof(raw[0]);
    if (bytesRead != remainingBytes) {
      portENTER_CRITICAL(&lock_);
      ++stats_.partialReads;
      portEXIT_CRITICAL(&lock_);
    }
    accumulatedSamples += samplesRead;
    if (accumulatedSamples != config::AUDIO_CAPTURE_FRAME_SAMPLES) continue;

    const int64_t processingStartedAtUs = esp_timer_get_time();
    for (size_t i = 0; i < accumulatedSamples; ++i) {
      // This exact conversion matches the validated INMP441 test sketch.
      frame.samples[i] = static_cast<int16_t>(raw[i] >> 16);
    }

    // The ring has one writer. Publish totalSamples_ only after the complete
    // frame is visible, keeping PSRAM writes outside the cross-core spinlock.
    uint64_t start;
    portENTER_CRITICAL(&lock_);
    start = totalSamples_;
    portEXIT_CRITICAL(&lock_);
    for (size_t i = 0; i < accumulatedSamples; ++i) {
      ring_[(start + i) % config::AUDIO_CAPTURE_RING_SAMPLES] =
          frame.samples[i];
    }
    const int64_t endedAtUs = esp_timer_get_time();
    portENTER_CRITICAL(&lock_);
    totalSamples_ += accumulatedSamples;
    frame.sequence = ++stats_.capturedFrames;
    stats_.capturedSamples += accumulatedSamples;
    frame.endSample = totalSamples_;
    frame.endedAtUs = endedAtUs;
    if (stats_.firstFrameEndedAtUs == 0) {
      stats_.firstFrameEndedAtUs = endedAtUs;
    } else {
      const uint32_t periodUs = static_cast<uint32_t>(
          endedAtUs - stats_.lastFrameEndedAtUs);
      stats_.minFramePeriodUs = min(stats_.minFramePeriodUs, periodUs);
      stats_.maxFramePeriodUs = max(stats_.maxFramePeriodUs, periodUs);
    }
    stats_.lastFrameEndedAtUs = endedAtUs;
    stats_.taskCore = xPortGetCoreID();
    stats_.stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
    portEXIT_CRITICAL(&lock_);

    frame.endedAt = static_cast<unsigned long>(endedAtUs / 1000);
    publishFrame(frame);
    const uint32_t workUs = static_cast<uint32_t>(
        esp_timer_get_time() - processingStartedAtUs);
    portENTER_CRITICAL(&lock_);
    stats_.busyTimeUs += workUs;
    stats_.maxFrameWorkUs = max(stats_.maxFrameWorkUs, workUs);
    portEXIT_CRITICAL(&lock_);
    accumulatedSamples = 0;
  }
}

void AudioCapture::publishFrame(const AudioFrame& frame) {
  if (soundQueue_ != nullptr) {
    sendNewest(soundQueue_, frame, stats_.soundDroppedFrames,
               stats_.soundQueueHighWater);
  }
  if (musicQueue_ != nullptr) {
    sendNewest(musicQueue_, frame, stats_.musicDroppedFrames,
               stats_.musicQueueHighWater);
  }
}

bool AudioCapture::sendNewest(QueueHandle_t queue, const AudioFrame& frame,
                              uint32_t& dropCounter,
                              uint32_t& queueHighWater) {
  if (xQueueSend(queue, &frame, 0) == pdTRUE) {
    const uint32_t depth = uxQueueMessagesWaiting(queue);
    portENTER_CRITICAL(&lock_);
    queueHighWater = max(queueHighWater, depth);
    portEXIT_CRITICAL(&lock_);
    return true;
  }
  AudioFrame stale;
  xQueueReceive(queue, &stale, 0);
  portENTER_CRITICAL(&lock_);
  ++dropCounter;
  portEXIT_CRITICAL(&lock_);
  const bool sent = xQueueSend(queue, &frame, 0) == pdTRUE;
  if (sent) {
    const uint32_t depth = uxQueueMessagesWaiting(queue);
    portENTER_CRITICAL(&lock_);
    queueHighWater = max(queueHighWater, depth);
    portEXIT_CRITICAL(&lock_);
  }
  return sent;
}
