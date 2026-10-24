#include "LightMonitor.h"

#include <cmath>

#include "Config.h"

void LightMonitor::addSample(float lux, unsigned long now) {
  if (!std::isfinite(lux) || lux < 0.0F || lux > config::LIGHT_MAX_VALID_LUX) {
    markInvalid();
    return;
  }

  snapshot_.valid = true;
  snapshot_.rawLux = lux;
  if (historyCount_ == 0) {
    snapshot_.filteredLux = lux;
  } else {
    snapshot_.filteredLux +=
        config::LIGHT_FILTER_ALPHA * (lux - snapshot_.filteredLux);
  }

  history_[historyIndex_] = snapshot_.filteredLux;
  historyIndex_ = (historyIndex_ + 1) % HISTORY_SIZE;
  if (historyCount_ < HISTORY_SIZE) ++historyCount_;

  const uint16_t recentCount =
      historyCount_ < RECENT_WINDOW_SAMPLES ? historyCount_
                                            : RECENT_WINDOW_SAMPLES;
  const uint16_t availableBaselineCount = historyCount_ - recentCount;
  const uint16_t baselineCount =
      availableBaselineCount < BASELINE_WINDOW_SAMPLES
          ? availableBaselineCount
          : BASELINE_WINDOW_SAMPLES;

  float recentValues[RECENT_WINDOW_SAMPLES];
  const uint16_t recentStart = historyCount_ - recentCount;
  for (uint16_t i = 0; i < recentCount; ++i) {
    recentValues[i] = historyValue(recentStart + i);
  }
  snapshot_.recentLux = median(recentValues, recentCount);

  float baselineValues[BASELINE_WINDOW_SAMPLES];
  const uint16_t baselineStart = recentStart - baselineCount;
  for (uint16_t i = 0; i < baselineCount; ++i) {
    baselineValues[i] = historyValue(baselineStart + i);
  }
  snapshot_.baselineLux = baselineCount > 0
                              ? median(baselineValues, baselineCount)
                              : snapshot_.recentLux;

  float deviations[BASELINE_WINDOW_SAMPLES];
  for (uint16_t i = 0; i < baselineCount; ++i) {
    deviations[i] =
        std::fabs(baselineValues[i] - snapshot_.baselineLux);
  }
  // 1.4826 converts median absolute deviation to a robust sigma estimate.
  snapshot_.noiseLux =
      baselineCount > 0 ? 1.4826F * median(deviations, baselineCount) : 0.0F;

  const float absoluteDrop = snapshot_.baselineLux - snapshot_.recentLux;
  snapshot_.relativeDrop =
      snapshot_.baselineLux > 0.01F && absoluteDrop > 0.0F
          ? absoluteDrop / snapshot_.baselineLux
          : 0.0F;
  const float relativeRequired =
      snapshot_.baselineLux * config::LIGHT_RELATIVE_DROP_RATIO;
  const float noiseRequired =
      snapshot_.noiseLux * config::LIGHT_NOISE_MULTIPLIER;
  snapshot_.requiredDropLux =
      relativeRequired > noiseRequired ? relativeRequired : noiseRequired;
  snapshot_.historySamples = historyCount_;
  snapshot_.darkening =
      baselineCount >= MIN_BASELINE_SAMPLES &&
      snapshot_.baselineLux >= config::LIGHT_RELATIVE_MIN_BASELINE_LUX &&
      absoluteDrop >= snapshot_.requiredDropLux;

  if (calibrationActive_ && calibrationCount_ < CALIBRATION_SIZE) {
    calibration_[calibrationCount_++] = lux;
  }
  (void)now;
}

void LightMonitor::markInvalid() {
  snapshot_.valid = false;
  snapshot_.darkening = false;
}

const LightSnapshot& LightMonitor::snapshot() const {
  return snapshot_;
}

void LightMonitor::startCalibration(unsigned long now) {
  calibrationActive_ = true;
  calibrationStartedAt_ = now;
  calibrationCount_ = 0;
}

LightCalibrationSummary LightMonitor::stopCalibration(unsigned long now) {
  LightCalibrationSummary result;
  result.samples = calibrationCount_;
  result.durationMs = now - calibrationStartedAt_;
  calibrationActive_ = false;
  if (calibrationCount_ == 0) return result;

  float sorted[CALIBRATION_SIZE];
  float sum = 0.0F;
  for (uint16_t i = 0; i < calibrationCount_; ++i) {
    sorted[i] = calibration_[i];
    sum += calibration_[i];
  }
  for (uint16_t i = 1; i < calibrationCount_; ++i) {
    const float value = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > value) {
      sorted[j + 1] = sorted[j];
      --j;
    }
    sorted[j + 1] = value;
  }

  result.valid = true;
  result.average = sum / calibrationCount_;
  result.minimum = sorted[0];
  result.maximum = sorted[calibrationCount_ - 1];
  const uint16_t middle = calibrationCount_ / 2;
  result.median = (calibrationCount_ & 1U)
                      ? sorted[middle]
                      : (sorted[middle - 1] + sorted[middle]) * 0.5F;
  return result;
}

bool LightMonitor::calibrationActive() const {
  return calibrationActive_;
}

float LightMonitor::median(float* values, uint16_t count) {
  if (count == 0) return 0.0F;
  for (uint16_t i = 1; i < count; ++i) {
    const float value = values[i];
    int j = static_cast<int>(i) - 1;
    while (j >= 0 && values[j] > value) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = value;
  }
  const uint16_t middle = count / 2;
  return (count & 1U) ? values[middle]
                      : (values[middle - 1] + values[middle]) * 0.5F;
}

float LightMonitor::historyValue(uint16_t chronologicalIndex) const {
  const uint16_t oldestIndex =
      historyCount_ < HISTORY_SIZE ? 0 : historyIndex_;
  return history_[(oldestIndex + chronologicalIndex) % HISTORY_SIZE];
}
