#pragma once

#include <Arduino.h>

#include "AudioCapture.h"
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
  uint32_t inputFrames = 0;
  uint32_t fftFrames = 0;
  uint64_t busyTimeUs = 0;
  uint32_t maxFftUs = 0;
  BaseType_t taskCore = tskNO_AFFINITY;
  UBaseType_t stackHighWaterMark = 0;
};

class MusicAnalyzer {
 public:
  bool begin(AudioCapture& capture);
  void setSuppressed(bool suppressed);
  MusicSnapshot snapshot() const;

 private:
  struct AnalysisFrame {
    int16_t samples[config::MUSIC_FFT_SIZE];
    unsigned long endedAt;
  };

  static void taskEntry(void* context);
  void task();
  void consumeInput(const AudioFrame& frame);
  void analyze(const AnalysisFrame& frame);
  void resetDetector();
  bool rhythmIsRegular(uint16_t& bpm, float& confidence) const;

  AudioCapture* capture_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  MusicSnapshot published_;
  bool suppressed_ = false;
  bool resetRequested_ = false;
  AnalysisFrame analysisFrame_ = {};
  size_t analysisFrameIndex_ = 0;
  float firHistory_[7] = {};
  uint8_t firIndex_ = 0;
  bool decimationPhase_ = false;

  float real_[config::MUSIC_FFT_SIZE] = {};
  float imag_[config::MUSIC_FFT_SIZE] = {};
  float smoothedBands_[config::MUSIC_BAND_COUNT] = {};
  float noiseBands_[config::MUSIC_BAND_COUNT] = {};
  float peakBands_[config::MUSIC_BAND_COUNT] = {};
  float previousBandEnergies_[config::MUSIC_BAND_COUNT] = {};
  float hannWindow_[config::MUSIC_FFT_SIZE] = {};
  uint8_t binBands_[config::MUSIC_FFT_SIZE / 2 + 1] = {};
  float noiseFloor_ = 0.0F;
  unsigned long lastBeatAt_ = 0;
  unsigned long lastBeatIntervalMs_ = 0;
  unsigned long lastSignalAt_ = 0;
  unsigned long lastMusicEvidenceAt_ = 0;
  unsigned long regularWindowStartedAt_ = 0;
  uint16_t regularWindowBpm_ = 0;
  uint8_t regularWindowCount_ = 0;
  unsigned long beatTimes_[config::MUSIC_REQUIRED_BEATS] = {};
  uint8_t beatCount_ = 0;
  uint8_t beatIndex_ = 0;
  bool active_ = false;
  uint32_t previousSequence_ = 0;
};
