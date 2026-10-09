#pragma once

#include <Arduino.h>

#include "Config.h"

struct MusicSnapshot {
  uint8_t bands[config::MUSIC_BAND_COUNT] = {};
  bool active = false;
  bool candidate = false;
  uint32_t beatSequence = 0;
  unsigned long lastBeatAt = 0;
  float beatStrength = 0.0F;
  float confidence = 0.0F;
  uint16_t bpm = 0;
  uint32_t droppedFrames = 0;
};

class MusicAnalyzer {
 public:
  bool begin();
  bool submitFrame(const uint16_t* samples, unsigned long endedAt,
                   uint32_t generation);
  void setSuppressed(bool suppressed, uint32_t generation);
  MusicSnapshot snapshot() const;

 private:
  struct AudioFrame {
    uint16_t samples[config::MUSIC_FFT_SIZE];
    unsigned long endedAt;
    uint32_t generation;
  };

  static void taskEntry(void* context);
  void task();
  void analyze(const AudioFrame& frame);
  void resetDetector();
  bool rhythmIsRegular(uint16_t& bpm, float& confidence) const;

  QueueHandle_t frameQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  MusicSnapshot published_;
  bool suppressed_ = false;
  uint32_t generation_ = 0;

  float real_[config::MUSIC_FFT_SIZE] = {};
  float imag_[config::MUSIC_FFT_SIZE] = {};
  float smoothedBands_[config::MUSIC_BAND_COUNT] = {};
  float noiseBands_[config::MUSIC_BAND_COUNT] = {};
  float peakBands_[config::MUSIC_BAND_COUNT] = {};
  float previousBandEnergies_[config::MUSIC_BAND_COUNT] = {};
  float noiseFloor_ = 0.0F;
  unsigned long lastBeatAt_ = 0;
  unsigned long lastBeatIntervalMs_ = 0;
  unsigned long lastStrongEnergyAt_ = 0;
  unsigned long lastRegularAt_ = 0;
  unsigned long lastLevelDebugAt_ = 0;
  unsigned long beatTimes_[config::MUSIC_REQUIRED_BEATS] = {};
  uint8_t beatCount_ = 0;
  uint8_t beatIndex_ = 0;
  bool active_ = false;
};
