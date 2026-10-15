#include "SoundDetector.h"

#include <cmath>
#include <esp_timer.h>

bool SoundDetector::begin(AudioCapture& capture) {
  capture_ = &capture;
  if (!capture.ready()) return false;
  if (xTaskCreatePinnedToCore(taskEntry, "deskbot-detect",
                              config::SOUND_TASK_STACK_WORDS, this,
                              config::SOUND_TASK_PRIORITY, &taskHandle_,
                              config::SOUND_TASK_CORE) != pdPASS) {
    return false;
  }
  return true;
}

SoundEvent SoundDetector::takeEvent() {
  portENTER_CRITICAL(&lock_);
  const SoundEvent result = pendingEvent_;
  pendingEvent_ = SoundEvent::None;
  portEXIT_CRITICAL(&lock_);
  return result;
}

void SoundDetector::setSuppressed(bool suppressed) {
  portENTER_CRITICAL(&lock_);
  const bool changed = suppressed_ != suppressed;
  suppressed_ = suppressed;
  if (changed) {
    pendingEvent_ = SoundEvent::None;
    resetRequested_ = true;
  }
  portEXIT_CRITICAL(&lock_);
}

void SoundDetector::cancelSoundEpisode() {
  portENTER_CRITICAL(&lock_);
  if (pendingEvent_ == SoundEvent::Sound) pendingEvent_ = SoundEvent::None;
  resetRequested_ = true;
  portEXIT_CRITICAL(&lock_);
}

SoundMetrics SoundDetector::metrics() const {
  portENTER_CRITICAL(&lock_);
  const SoundMetrics result = metrics_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

void SoundDetector::taskEntry(void* context) {
  static_cast<SoundDetector*>(context)->task();
}

void SoundDetector::task() {
  AudioFrame frame;
  for (;;) {
    if (capture_->receiveSoundFrame(frame, portMAX_DELAY)) analyze(frame);
  }
}

void SoundDetector::analyze(const AudioFrame& frame) {
  const int64_t startedAtUs = esp_timer_get_time();
  portENTER_CRITICAL(&lock_);
  const bool reset = resetRequested_;
  resetRequested_ = false;
  portEXIT_CRITICAL(&lock_);
  const uint32_t gap = previousSequence_ != 0 && frame.sequence > previousSequence_
                           ? frame.sequence - previousSequence_ - 1
                           : 0;
  previousSequence_ = frame.sequence;
  if (reset || gap != 0) resetHistory();
  const bool wasClapCandidate = clapCandidate_;
  SoundEvent detectedEvent = SoundEvent::None;
  int16_t minimum = INT16_MAX;
  int16_t maximum = INT16_MIN;
  int64_t sampleSum = 0;
  for (int16_t sample : frame.samples) {
    minimum = min(minimum, sample);
    maximum = max(maximum, sample);
    sampleSum += sample;
  }
  const float dcOffset =
      static_cast<float>(sampleSum) / config::AUDIO_CAPTURE_FRAME_SAMPLES;
  double sumSquares = 0.0;
  for (int16_t sample : frame.samples) {
    const double centered = static_cast<double>(sample) - dcOffset;
    sumSquares += centered * centered;
  }
  const float rms = sqrtf(static_cast<float>(
      sumSquares / config::AUDIO_CAPTURE_FRAME_SAMPLES));
  const int32_t peakToPeak = static_cast<int32_t>(maximum) - minimum;

  portENTER_CRITICAL(&lock_);
  const bool suppressed = suppressed_;
  float noiseFloor = metrics_.noiseFloor;
  portEXIT_CRITICAL(&lock_);
  if (noiseFloor <= 1.0F) noiseFloor = rms;
  const float noiseRate = rms < noiseFloor ? config::SOUND_NOISE_RELEASE
                                           : config::SOUND_NOISE_ATTACK;
  noiseFloor += noiseRate * (rms - noiseFloor);
  const float energy = rms * rms;
  const unsigned long now = frame.endedAt;
  const bool cooldown = hasEvent_ &&
      now - lastEventAt_ < config::AUDIO_EVENT_COOLDOWN_MS;
  const bool soundActive =
      rms >= config::SOUND_MIN_RMS &&
      rms >= noiseFloor * config::SOUND_NOISE_MULTIPLIER;
  if (!dcBaselineInitialized_) {
    dcBaseline_ = dcOffset;
    dcBaselineInitialized_ = true;
  }
  const float dcExcursion = fabsf(dcOffset - dcBaseline_);
  if (!soundActive && !clapCandidate_ && !eventLatched_) {
    dcBaseline_ +=
        config::SOUND_DC_BASELINE_RATE * (dcOffset - dcBaseline_);
  }

  if (soundActive) {
    inactiveFrames_ = 0;
    if (soundEpisodeActive_) soundLastActiveAt_ = now;
  } else if (eventLatched_) {
    if (inactiveFrames_ < config::SOUND_RELEASE_FRAMES) ++inactiveFrames_;
    if (inactiveFrames_ >= config::SOUND_RELEASE_FRAMES) {
      eventLatched_ = false;
      inactiveFrames_ = 0;
    }
  }

  soundHistory_[soundHistoryIndex_] = soundActive;
  soundHistoryIndex_ =
      (soundHistoryIndex_ + 1) % config::SOUND_HISTORY_FRAMES;
  if (soundHistoryCount_ < config::SOUND_HISTORY_FRAMES) ++soundHistoryCount_;

  const float rise = rms - previousRms_;
  const float peakToRmsRatio =
      rms > 1.0F ? static_cast<float>(peakToPeak) / rms : 0.0F;
  const bool clapOnset =
      !suppressed && !cooldown && !eventLatched_ && !clapCandidate_ &&
      peakToPeak >= config::CLAP_MIN_PEAK &&
      peakToPeak >= noiseFloor * config::CLAP_NOISE_MULTIPLIER &&
      rise >= config::CLAP_MIN_RISE;
  if (clapOnset) {
    if (peakToRmsRatio >= config::CLAP_MIN_PEAK_TO_RMS_RATIO) {
      clapCandidate_ = true;
      clapPeakRms_ = rms;
      clapMaxDcExcursion_ = dcExcursion;
      clapStartedAt_ = now;
    } else {
      resetHistory();
      eventLatched_ = true;
      if (config::MIC_DEBUG) {
        Serial.printf(
            "MIC_DEBUG: CLAP_REJECTED reason=SHAPE ratio=%.2f limit=%.2f\n",
            peakToRmsRatio, config::CLAP_MIN_PEAK_TO_RMS_RATIO);
      }
    }
  }
  if (clapCandidate_) {
    clapPeakRms_ = max(clapPeakRms_, rms);
    clapMaxDcExcursion_ = max(clapMaxDcExcursion_, dcExcursion);
    const unsigned long duration = now - clapStartedAt_;
    if (duration > config::CLAP_MAX_DURATION_MS) {
      clapCandidate_ = false;
      clapPeakRms_ = 0.0F;
      clapMaxDcExcursion_ = 0.0F;
    } else if (rms <= clapPeakRms_ * config::CLAP_DECAY_RATIO) {
      const float candidateDcExcursion = clapMaxDcExcursion_;
      const bool mechanicalImpact =
          candidateDcExcursion > config::CLAP_MAX_DC_EXCURSION;
      clapCandidate_ = false;
      clapPeakRms_ = 0.0F;
      clapMaxDcExcursion_ = 0.0F;
      if (!suppressed && !cooldown && !mechanicalImpact) {
        soundEpisodeActive_ = false;
        publish(SoundEvent::Clap);
        detectedEvent = SoundEvent::Clap;
        hasEvent_ = true;
        lastEventAt_ = now;
        resetHistory();
        eventLatched_ = true;
      } else if (mechanicalImpact) {
        resetHistory();
        eventLatched_ = true;
        if (config::MIC_DEBUG) {
          Serial.printf(
              "MIC_DEBUG: CLAP_REJECTED reason=DC excursion=%.0f limit=%.0f\n",
              candidateDcExcursion, config::CLAP_MAX_DC_EXCURSION);
        }
      }
    }
  }

  if (!suppressed && !soundEpisodeActive_ && !cooldown && !eventLatched_ &&
      !clapCandidate_ &&
      soundHistoryCount_ == config::SOUND_HISTORY_FRAMES) {
    uint8_t activeCount = 0;
    for (bool active : soundHistory_) activeCount += active ? 1 : 0;
    if (activeCount >= config::SOUND_REQUIRED_ACTIVE_FRAMES) {
      resetHistory();
      soundEpisodeActive_ = true;
      soundEpisodeStartedAt_ = now;
      soundLastActiveAt_ = now;
      Serial.println("MIC: SOUND EPISODE START");
    }
  }

  if (!suppressed && soundEpisodeActive_ && !clapCandidate_ &&
      !soundActive && now - soundLastActiveAt_ >= config::SOUND_END_SILENCE_MS) {
    const unsigned long duration = now - soundEpisodeStartedAt_;
    const unsigned long silence = now - soundLastActiveAt_;
    soundEpisodeActive_ = false;
    publish(SoundEvent::Sound);
    detectedEvent = SoundEvent::Sound;
    hasEvent_ = true;
    lastEventAt_ = now;
    Serial.printf("MIC: SOUND EPISODE END duration=%lums silence=%lums\n",
                  duration, silence);
    resetHistory();
    eventLatched_ = true;
  }

  previousRms_ = rms;
  portENTER_CRITICAL(&lock_);
  metrics_.rms = rms;
  metrics_.dcOffset = dcOffset;
  metrics_.dcBaseline = dcBaseline_;
  metrics_.dcExcursion = dcExcursion;
  metrics_.peakToPeak = peakToPeak;
  metrics_.shortEnergy = energy;
  metrics_.noiseFloor = noiseFloor;
  metrics_.clapCandidate = clapCandidate_;
  ++metrics_.processedFrames;
  metrics_.sequenceGaps += gap;
  const uint32_t elapsedUs =
      static_cast<uint32_t>(esp_timer_get_time() - startedAtUs);
  metrics_.busyTimeUs += elapsedUs;
  metrics_.maxAnalyzeUs = max(metrics_.maxAnalyzeUs, elapsedUs);
  metrics_.taskCore = xPortGetCoreID();
  metrics_.stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
  portEXIT_CRITICAL(&lock_);

  if (config::MIC_DEBUG) {
    if (soundActive && !previousSoundActive_ && !eventLatched_) {
      Serial.printf(
          "MIC_DEBUG: ACTIVE rms=%.0f peak=%ld noise=%.0f dc=%.0f dc_delta=%.0f\n",
          rms, static_cast<long>(peakToPeak), noiseFloor, dcOffset,
          dcExcursion);
    }
    if (!wasClapCandidate && clapCandidate_) {
      Serial.printf(
          "MIC_DEBUG: CLAP_CANDIDATE rms=%.0f peak=%ld ratio=%.2f rise=%.0f noise=%.0f dc=%.0f dc_delta=%.0f\n",
          rms, static_cast<long>(peakToPeak), peakToRmsRatio, rise,
          noiseFloor, dcOffset, dcExcursion);
    }
    if (detectedEvent != SoundEvent::None) {
      Serial.printf("MIC_DEBUG: EVENT=%s rms=%.0f peak=%ld noise=%.0f dc=%.0f dc_delta=%.0f\n",
                    detectedEvent == SoundEvent::Clap ? "CLAP" : "SOUND",
                    rms, static_cast<long>(peakToPeak), noiseFloor, dcOffset,
                    dcExcursion);
    }
  }
  previousSoundActive_ = soundActive;
}

void SoundDetector::resetHistory() {
  for (bool& active : soundHistory_) active = false;
  soundHistoryIndex_ = 0;
  soundHistoryCount_ = 0;
  clapCandidate_ = false;
  clapPeakRms_ = 0.0F;
  clapMaxDcExcursion_ = 0.0F;
  clapStartedAt_ = 0;
  previousRms_ = 0.0F;
  eventLatched_ = false;
  inactiveFrames_ = 0;
  previousSoundActive_ = false;
  soundEpisodeActive_ = false;
  soundEpisodeStartedAt_ = 0;
  soundLastActiveAt_ = 0;
}

void SoundDetector::publish(SoundEvent event) {
  portENTER_CRITICAL(&lock_);
  // CLAP has priority over an unconsumed SOUND event.
  if (pendingEvent_ == SoundEvent::None || event == SoundEvent::Clap) {
    pendingEvent_ = event;
  }
  portEXIT_CRITICAL(&lock_);
}
