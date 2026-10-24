#pragma once

#include <Arduino.h>

#include "AudioCapture.h"

enum class SoundEvent : uint8_t {
  None,
  Sound,
  Clap,
};

struct SoundMetrics {
  float rms = 0.0F;
  float dcOffset = 0.0F;
  float dcBaseline = 0.0F;
  float dcExcursion = 0.0F;
  int32_t peakToPeak = 0;
  float shortEnergy = 0.0F;
  float noiseFloor = 0.0F;
  bool clapCandidate = false;
  uint32_t processedFrames = 0;
  uint32_t sequenceGaps = 0;
  uint64_t busyTimeUs = 0;
  uint32_t maxAnalyzeUs = 0;
  BaseType_t taskCore = tskNO_AFFINITY;
  UBaseType_t stackHighWaterMark = 0;
};

class SoundDetector {
 public:
  bool begin(AudioCapture& capture);
  SoundEvent takeEvent();
  void setSuppressed(bool suppressed);
  // Drops an in-flight ordinary sound episode when a higher-priority event
  // (voice, music, motion, sleep, or local playback) takes ownership.
  void cancelSoundEpisode();
  SoundMetrics metrics() const;

 private:
  static void taskEntry(void* context);
  void task();
  void analyze(const AudioFrame& frame);
  void resetHistory();
  void publish(SoundEvent event);

  AudioCapture* capture_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  SoundMetrics metrics_;
  SoundEvent pendingEvent_ = SoundEvent::None;
  bool suppressed_ = false;
  bool resetRequested_ = false;
  bool soundHistory_[config::SOUND_HISTORY_FRAMES] = {};
  uint8_t soundHistoryIndex_ = 0;
  uint8_t soundHistoryCount_ = 0;
  float previousRms_ = 0.0F;
  float clapPeakRms_ = 0.0F;
  float clapMaxDcExcursion_ = 0.0F;
  float dcBaseline_ = 0.0F;
  bool dcBaselineInitialized_ = false;
  bool clapCandidate_ = false;
  unsigned long clapStartedAt_ = 0;
  unsigned long lastEventAt_ = 0;
  bool hasEvent_ = false;
  bool eventLatched_ = false;
  uint8_t inactiveFrames_ = 0;
  bool previousSoundActive_ = false;
  bool soundEpisodeActive_ = false;
  unsigned long soundEpisodeStartedAt_ = 0;
  unsigned long soundLastActiveAt_ = 0;
  uint32_t previousSequence_ = 0;
};
