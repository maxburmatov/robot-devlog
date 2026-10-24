#pragma once

#include <Arduino.h>

struct LightSnapshot {
  bool valid = false;
  float rawLux = 0.0F;
  float filteredLux = 0.0F;
  float recentLux = 0.0F;
  float baselineLux = 0.0F;
  float noiseLux = 0.0F;
  float requiredDropLux = 0.0F;
  float relativeDrop = 0.0F;
  bool darkening = false;
  uint16_t historySamples = 0;
};

struct LightCalibrationSummary {
  bool valid = false;
  uint16_t samples = 0;
  float average = 0.0F;
  float median = 0.0F;
  float minimum = 0.0F;
  float maximum = 0.0F;
  unsigned long durationMs = 0;
};

class LightMonitor {
 public:
  void addSample(float lux, unsigned long now);
  void markInvalid();
  const LightSnapshot& snapshot() const;

  void startCalibration(unsigned long now);
  LightCalibrationSummary stopCalibration(unsigned long now);
  bool calibrationActive() const;

 private:
  static constexpr uint16_t HISTORY_SIZE = 120;
  // LIGHT_INTERVAL_MS is 500 ms: compare the latest 3-second median with
  // the median of the preceding 20 seconds.
  static constexpr uint16_t RECENT_WINDOW_SAMPLES = 6;
  static constexpr uint16_t BASELINE_WINDOW_SAMPLES = 40;
  static constexpr uint16_t MIN_BASELINE_SAMPLES = 20;
  static constexpr uint16_t CALIBRATION_SIZE = 120;

  static float median(float* values, uint16_t count);
  float historyValue(uint16_t chronologicalIndex) const;

  float history_[HISTORY_SIZE] = {};
  uint16_t historyCount_ = 0;
  uint16_t historyIndex_ = 0;
  LightSnapshot snapshot_;

  bool calibrationActive_ = false;
  unsigned long calibrationStartedAt_ = 0;
  float calibration_[CALIBRATION_SIZE] = {};
  uint16_t calibrationCount_ = 0;
};
