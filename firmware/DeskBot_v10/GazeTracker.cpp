#include "GazeTracker.h"

#include <cmath>

namespace {

float clampFloat(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

}  // namespace

void GazeTracker::begin(uint32_t nowMs) {
  enabled_ = config::GAZE_TRACKING_ENABLED_BY_DEFAULT;
  paused_ = false;
  hasTarget_ = false;
  requestPending_ = false;
  requestTaken_ = false;
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  state_ = enabled_ ? GazeTrackerState::Idle : GazeTrackerState::Disabled;
  nextScanAtMs_ = nowMs;
}

uint32_t GazeTracker::randomDelay(uint32_t minimumMs,
                                  uint32_t maximumMs) const {
  if (maximumMs <= minimumMs) return minimumMs;
  return static_cast<uint32_t>(
      random(static_cast<long>(minimumMs), static_cast<long>(maximumMs + 1)));
}

float GazeTracker::correctedYaw(float yawDeg) const {
  return clampFloat(yawDeg - config::GAZE_SENSOR_YAW_OFFSET_DEG,
                    config::NECK_YAW_MIN_DEG, config::NECK_YAW_MAX_DEG);
}

void GazeTracker::resetPoint(GazeScanPoint& point, float yawDeg) {
  point = {};
  point.commandedYawDeg = clampFloat(yawDeg, config::NECK_YAW_MIN_DEG,
                                    config::NECK_YAW_MAX_DEG);
}

void GazeTracker::startOperation(Operation operation, const float* angles,
                                 size_t count, uint32_t nowMs) {
  operation_ = operation;
  operationPointCount_ = count > kMaximumPoints ? kMaximumPoints : count;
  operationPointIndex_ = 0;
  for (size_t i = 0; i < operationPointCount_; ++i) {
    operationAngles_[i] = clampFloat(angles[i], config::NECK_YAW_MIN_DEG,
                                     config::NECK_YAW_MAX_DEG);
    resetPoint(workPoints_[i], angles[i]);
  }
  sampleAttempts_ = 0;
  validSampleCount_ = 0;
  queueCurrentPoint();
  if (debug_) {
    Serial.printf("GAZE: operation=%u points=%u at=%lu\n",
                  static_cast<unsigned>(operation_),
                  static_cast<unsigned>(operationPointCount_),
                  static_cast<unsigned long>(nowMs));
  }
}

void GazeTracker::startFullScan(uint32_t nowMs) {
  float angles[kMaximumPoints];
  for (size_t i = 0; i < kMaximumPoints; ++i) {
    const size_t source = reverseScan_ ? kMaximumPoints - 1 - i : i;
    angles[i] = correctedYaw(config::GAZE_SCAN_YAW_DEG[source]);
  }
  Serial.printf("GAZE: scan started direction=%s\n",
                reverseScan_ ? "RIGHT_TO_LEFT" : "LEFT_TO_RIGHT");
  startOperation(Operation::FullScan, angles, kMaximumPoints, nowMs);
}

void GazeTracker::queueCurrentPoint() {
  if (operationPointIndex_ >= operationPointCount_) return;
  pendingRequest_.hasTarget = true;
  pendingRequest_.yawDeg = operationAngles_[operationPointIndex_];
  pendingRequest_.pitchDeg = config::NECK_PITCH_CENTER_DEG;
  pendingRequest_.speedDegPerSec = config::GAZE_SCAN_SPEED_DEG_PER_SEC;
  requestPending_ = true;
  requestTaken_ = false;
  phase_ = PointPhase::WaitingForRequest;
  setPublicStateForPhase();
}

void GazeTracker::queuePoseRequest(float yawDeg, float pitchDeg,
                                   float speedDegPerSec) {
  pendingRequest_.hasTarget = true;
  pendingRequest_.yawDeg = clampFloat(yawDeg, config::NECK_YAW_MIN_DEG,
                                     config::NECK_YAW_MAX_DEG);
  pendingRequest_.pitchDeg = clampFloat(pitchDeg, config::NECK_PITCH_MIN_DEG,
                                       config::NECK_PITCH_MAX_DEG);
  pendingRequest_.speedDegPerSec = speedDegPerSec;
  requestPending_ = true;
  requestTaken_ = false;
  lockedReady_ = false;
  lockedStableSinceMs_ = 0;
}

void GazeTracker::queueHoldRequest(float yawDeg) {
  queuePoseRequest(yawDeg, config::NECK_PITCH_CENTER_DEG,
                   config::GAZE_SCAN_SPEED_DEG_PER_SEC);
}

void GazeTracker::scheduleNextPitchGlance(uint32_t nowMs) {
  nextPitchGlanceAtMs_ =
      nowMs + randomDelay(config::GAZE_PITCH_GLANCE_MIN_INTERVAL_MS,
                          config::GAZE_PITCH_GLANCE_MAX_INTERVAL_MS);
}

void GazeTracker::startPitchGlance(uint32_t nowMs) {
  pitchGlanceLooksUp_ = !pitchGlanceLooksUp_;
  const float offset = pitchGlanceLooksUp_ ? config::GAZE_PITCH_GLANCE_DEG
                                          : -config::GAZE_PITCH_GLANCE_DEG;
  pitchGlancePhase_ = PitchGlancePhase::Away;
  pitchGlanceReadyAtMs_ = 0;
  queuePoseRequest(targetYawDeg_, config::NECK_PITCH_CENTER_DEG + offset,
                   config::GAZE_PITCH_GLANCE_SPEED_DEG_PER_SEC);
  if (debug_) {
    Serial.printf("GAZE: pitch glance offset=%.1f at=%lu\n", offset,
                  static_cast<unsigned long>(nowMs));
  }
}

void GazeTracker::setPublicStateForPhase() {
  if (operation_ == Operation::Confirmation) {
    state_ = GazeTrackerState::Confirming;
  } else if (operation_ == Operation::Tracking ||
             operation_ == Operation::Reacquire) {
    state_ = GazeTrackerState::LocalSearch;
  } else if (phase_ == PointPhase::Settling) {
    state_ = GazeTrackerState::Settling;
  } else if (phase_ == PointPhase::Sampling) {
    state_ = GazeTrackerState::Sampling;
  } else {
    state_ = GazeTrackerState::MoveToScanPoint;
  }
}

bool GazeTracker::takeRequest(GazeRequest& request) {
  if (!requestPending_ || paused_ || !enabled_) return false;
  request = pendingRequest_;
  requestPending_ = false;
  requestTaken_ = true;
  if (operation_ != Operation::None) {
    phase_ = PointPhase::WaitingForArrival;
    setPublicStateForPhase();
  }
  return true;
}

void GazeTracker::update(uint32_t nowMs, float currentYaw, bool neckMoving) {
  currentYawDeg_ = currentYaw;
  if (!enabled_ || paused_) return;

  if (state_ == GazeTrackerState::Evaluating) {
    evaluateFullScan(nowMs, currentYaw);
    return;
  }

  if (state_ == GazeTrackerState::Lost) {
    if (!requestPending_ && !neckMoving &&
        fabsf(currentYaw - config::NECK_YAW_CENTER_DEG) <=
            config::GAZE_YAW_TOLERANCE_DEG) {
      state_ = GazeTrackerState::Idle;
    }
    return;
  }

  if (state_ == GazeTrackerState::Idle) {
    if (forceScan_ || deadlineReached(nowMs, nextScanAtMs_)) {
      forceScan_ = false;
      startFullScan(nowMs);
    }
    return;
  }

  if (state_ == GazeTrackerState::Locked) {
    const bool atTarget =
        fabsf(currentYaw - targetYawDeg_) <= config::GAZE_YAW_TOLERANCE_DEG;
    if (requestPending_ || neckMoving || !atTarget) {
      lockedReady_ = false;
      lockedStableSinceMs_ = 0;
    } else if (lockedStableSinceMs_ == 0) {
      lockedStableSinceMs_ = nowMs;
    } else if (nowMs - lockedStableSinceMs_ >= config::GAZE_SERVO_SETTLE_MS) {
      lockedReady_ = true;
    }
    if (!lockedReady_) return;

    if (pitchGlancePhase_ == PitchGlancePhase::Away) {
      if (pitchGlanceReadyAtMs_ == 0) {
        pitchGlanceReadyAtMs_ = nowMs + config::GAZE_PITCH_GLANCE_HOLD_MS;
      } else if (deadlineReached(nowMs, pitchGlanceReadyAtMs_)) {
        pitchGlancePhase_ = PitchGlancePhase::Returning;
        pitchGlanceReadyAtMs_ = 0;
        queuePoseRequest(targetYawDeg_, config::NECK_PITCH_CENTER_DEG,
                         config::GAZE_PITCH_GLANCE_SPEED_DEG_PER_SEC);
      }
      return;
    }

    if (pitchGlancePhase_ == PitchGlancePhase::Returning) {
      pitchGlancePhase_ = PitchGlancePhase::None;
      nextTrackAtMs_ = nowMs + config::GAZE_TRACK_INTERVAL_MS;
      scheduleNextPitchGlance(nowMs);
      return;
    }

    if (deadlineReached(nowMs, nextTrackAtMs_)) {
      const float angles[] = {
          targetYawDeg_, targetYawDeg_ - config::GAZE_LOCAL_PROBE_DEG,
          targetYawDeg_ + config::GAZE_LOCAL_PROBE_DEG,
      };
      startOperation(Operation::Tracking, angles, 3, nowMs);
    } else if (deadlineReached(nowMs, nextPitchGlanceAtMs_)) {
      startPitchGlance(nowMs);
    }
    return;
  }

  if (operation_ == Operation::None ||
      phase_ == PointPhase::WaitingForRequest) {
    return;
  }

  const float requestedYaw = operationAngles_[operationPointIndex_];
  const bool atPoint = fabsf(currentYaw - requestedYaw) <=
                       config::GAZE_YAW_TOLERANCE_DEG;
  if (phase_ == PointPhase::WaitingForArrival) {
    if (!neckMoving && atPoint && requestTaken_) {
      phase_ = PointPhase::Settling;
      settleStartedAtMs_ = nowMs;
      setPublicStateForPhase();
    }
    return;
  }

  if (phase_ == PointPhase::Settling) {
    if (neckMoving || !atPoint) {
      phase_ = PointPhase::WaitingForArrival;
      setPublicStateForPhase();
    } else if (nowMs - settleStartedAtMs_ >= config::GAZE_SERVO_SETTLE_MS) {
      phase_ = PointPhase::Sampling;
      samplingStartedAtMs_ = nowMs;
      sampleAttempts_ = 0;
      validSampleCount_ = 0;
      setPublicStateForPhase();
    }
  }
}

uint16_t GazeTracker::medianOfValidSamples() const {
  uint16_t sorted[config::GAZE_SAMPLES_PER_POINT] = {};
  for (uint8_t i = 0; i < validSampleCount_; ++i) sorted[i] = validSamples_[i];
  for (uint8_t i = 1; i < validSampleCount_; ++i) {
    const uint16_t value = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > value) {
      sorted[j + 1] = sorted[j];
      --j;
    }
    sorted[j + 1] = value;
  }
  if (validSampleCount_ == 0) return 0;
  if ((validSampleCount_ & 1U) != 0) return sorted[validSampleCount_ / 2];
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(sorted[validSampleCount_ / 2 - 1]) +
       sorted[validSampleCount_ / 2]) /
      2U);
}

void GazeTracker::onDistanceMeasurement(
    const GazeMeasurement& measurement) {
  if (!enabled_ || paused_) return;

  if (state_ == GazeTrackerState::Locked) {
    if (!lockedReady_ || pitchGlancePhase_ != PitchGlancePhase::None) return;
    if (measurement.valid &&
        measurement.distanceMm <= config::GAZE_TARGET_MAX_DISTANCE_MM) {
      consecutiveLostSamples_ = 0;
      targetDistanceMm_ = measurement.distanceMm;
      targetSeenAtMs_ = measurement.timestampMs;
    } else if (++consecutiveLostSamples_ >=
               config::GAZE_LOST_CONFIRM_SAMPLES) {
      consecutiveLostSamples_ = 0;
      const float angles[] = {
          targetYawDeg_, targetYawDeg_ - config::GAZE_LOCAL_REACQUIRE_DEG,
          targetYawDeg_ + config::GAZE_LOCAL_REACQUIRE_DEG,
      };
      startOperation(Operation::Reacquire, angles, 3,
                     measurement.timestampMs);
    }
    return;
  }

  if (phase_ != PointPhase::Sampling ||
      static_cast<int32_t>(measurement.timestampMs - samplingStartedAtMs_) <
          0 ||
      operationPointIndex_ >= operationPointCount_) {
    return;
  }

  ++sampleAttempts_;
  if (measurement.valid &&
      measurement.distanceMm >= config::DISTANCE_MIN_VALID_MM &&
      validSampleCount_ < config::GAZE_SAMPLES_PER_POINT) {
    validSamples_[validSampleCount_++] = measurement.distanceMm;
  }
  if (sampleAttempts_ >= config::GAZE_SAMPLES_PER_POINT) {
    finishCurrentPoint(measurement.timestampMs);
  }
}

void GazeTracker::finishCurrentPoint(uint32_t nowMs) {
  GazeScanPoint& point = workPoints_[operationPointIndex_];
  point.actualYawDeg = currentYawDeg_;
  point.totalSamples = sampleAttempts_;
  point.validSamples = validSampleCount_;
  point.measuredAtMs = nowMs;
  if (validSampleCount_ > 0) {
    point.minimumDistanceMm = validSamples_[0];
    point.maximumDistanceMm = validSamples_[0];
    for (uint8_t i = 1; i < validSampleCount_; ++i) {
      if (validSamples_[i] < point.minimumDistanceMm)
        point.minimumDistanceMm = validSamples_[i];
      if (validSamples_[i] > point.maximumDistanceMm)
        point.maximumDistanceMm = validSamples_[i];
    }
    point.medianDistanceMm = medianOfValidSamples();
  }

  if (++operationPointIndex_ < operationPointCount_) {
    sampleAttempts_ = 0;
    validSampleCount_ = 0;
    queueCurrentPoint();
    return;
  }

  phase_ = PointPhase::None;
  requestTaken_ = false;
  if (operation_ == Operation::FullScan) {
    for (size_t i = 0; i < kMaximumPoints; ++i) {
      const size_t destination = reverseScan_ ? kMaximumPoints - 1 - i : i;
      scanMap_[destination] = workPoints_[i];
    }
    reverseScan_ = !reverseScan_;
    operation_ = Operation::None;
    state_ = GazeTrackerState::Evaluating;
  } else if (operation_ == Operation::Confirmation) {
    const GazeScanPoint confirmation = workPoints_[0];
    operation_ = Operation::None;
    if (pointIsCandidate(confirmation)) {
      lockTarget(candidateYawDeg_, confirmation.medianDistanceMm, nowMs, true);
    } else {
      loseTarget(nowMs);
    }
  } else {
    evaluateLocalSearch(nowMs);
  }
}

bool GazeTracker::pointIsCandidate(const GazeScanPoint& point) const {
  return point.validSamples >= config::GAZE_REQUIRED_VALID_SAMPLES &&
         point.medianDistanceMm >= config::DISTANCE_MIN_VALID_MM &&
         point.medianDistanceMm <= config::GAZE_TARGET_MAX_DISTANCE_MM;
}

int GazeTracker::selectBestPoint(const GazeScanPoint* points, size_t count,
                                 float referenceYaw) const {
  int best = -1;
  for (size_t i = 0; i < count; ++i) {
    if (!pointIsCandidate(points[i])) continue;
    if (best < 0) {
      best = static_cast<int>(i);
      continue;
    }
    const uint16_t distance = points[i].medianDistanceMm;
    const uint16_t bestDistance = points[best].medianDistanceMm;
    if (distance + config::GAZE_DISTANCE_HYSTERESIS_MM < bestDistance ||
        (abs(static_cast<int>(distance) - static_cast<int>(bestDistance)) <=
             config::GAZE_DISTANCE_HYSTERESIS_MM &&
         fabsf(points[i].commandedYawDeg - referenceYaw) <
             fabsf(points[best].commandedYawDeg - referenceYaw))) {
      best = static_cast<int>(i);
    }
  }
  return best;
}

void GazeTracker::evaluateFullScan(uint32_t nowMs, float currentYaw) {
  const int best = selectBestPoint(scanMap_, kMaximumPoints, currentYaw);
  Serial.println("GAZE: scan finished");
  if (best < 0) {
    loseTarget(nowMs);
    return;
  }

  size_t first = static_cast<size_t>(best);
  size_t last = first;
  while (first > 0 && pointIsCandidate(scanMap_[first - 1])) --first;
  while (last + 1 < kMaximumPoints && pointIsCandidate(scanMap_[last + 1]))
    ++last;

  float weightedYaw = 0.0F;
  float weightSum = 0.0F;
  for (size_t i = first; i <= last; ++i) {
    const float boundedDistance =
        clampFloat(scanMap_[i].medianDistanceMm,
                   config::DISTANCE_MIN_VALID_MM,
                   config::GAZE_TARGET_MAX_DISTANCE_MM);
    const float weight = 1.0F / boundedDistance;
    weightedYaw += scanMap_[i].commandedYawDeg * weight;
    weightSum += weight;
  }
  candidateYawDeg_ = weightSum > 0.0F
                         ? weightedYaw / weightSum
                         : scanMap_[best].commandedYawDeg;
  const float confirmationAngles[] = {candidateYawDeg_};
  startOperation(Operation::Confirmation, confirmationAngles, 1, nowMs);
}

void GazeTracker::evaluateLocalSearch(uint32_t nowMs) {
  const Operation completedOperation = operation_;
  int best = -1;
  for (size_t i = 0; i < operationPointCount_; ++i) {
    if (!pointIsCandidate(workPoints_[i])) continue;
    if (best < 0 ||
        workPoints_[i].medianDistanceMm <
            workPoints_[best].medianDistanceMm ||
        (workPoints_[i].medianDistanceMm ==
             workPoints_[best].medianDistanceMm &&
         fabsf(workPoints_[i].commandedYawDeg - targetYawDeg_) <
             fabsf(workPoints_[best].commandedYawDeg - targetYawDeg_))) {
      best = static_cast<int>(i);
    }
  }
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  if (best < 0) {
    if (completedOperation == Operation::Tracking) {
      const float angles[] = {
          targetYawDeg_, targetYawDeg_ - config::GAZE_LOCAL_REACQUIRE_DEG,
          targetYawDeg_ + config::GAZE_LOCAL_REACQUIRE_DEG,
      };
      startOperation(Operation::Reacquire, angles, 3, nowMs);
    } else {
      loseTarget(nowMs);
    }
    return;
  }

  const GazeScanPoint& bestPoint = workPoints_[best];
  const bool currentValid = pointIsCandidate(workPoints_[0]);
  const bool meaningfulChange =
      fabsf(bestPoint.commandedYawDeg - targetYawDeg_) >=
      config::GAZE_MIN_YAW_CHANGE_DEG;
  const bool clearImprovement =
      !currentValid ||
      bestPoint.medianDistanceMm + config::GAZE_DISTANCE_HYSTERESIS_MM <
          workPoints_[0].medianDistanceMm;

  if (meaningfulChange &&
      fabsf(proposedYawDeg_ - bestPoint.commandedYawDeg) <=
          config::GAZE_YAW_TOLERANCE_DEG) {
    if (proposedDirectionCount_ < 255) ++proposedDirectionCount_;
  } else if (meaningfulChange) {
    proposedYawDeg_ = bestPoint.commandedYawDeg;
    proposedDirectionCount_ = 1;
  } else {
    proposedDirectionCount_ = 0;
  }

  if (meaningfulChange &&
      (clearImprovement || proposedDirectionCount_ >= 2)) {
    targetYawDeg_ = bestPoint.commandedYawDeg;
    proposedDirectionCount_ = 0;
    Serial.printf("GAZE: target direction changed yaw=%.1f\n", targetYawDeg_);
  }
  targetDistanceMm_ = bestPoint.medianDistanceMm;
  targetSeenAtMs_ = nowMs;
  consecutiveLostSamples_ = 0;
  state_ = GazeTrackerState::Locked;
  nextTrackAtMs_ = nowMs + config::GAZE_TRACK_INTERVAL_MS;
  scheduleNextPitchGlance(nowMs);
  queueHoldRequest(targetYawDeg_);
}

void GazeTracker::lockTarget(float yawDeg, uint16_t distanceMm,
                             uint32_t nowMs, bool announce) {
  hasTarget_ = true;
  targetYawDeg_ = clampFloat(yawDeg, config::NECK_YAW_MIN_DEG,
                             config::NECK_YAW_MAX_DEG);
  targetDistanceMm_ = distanceMm;
  targetSeenAtMs_ = nowMs;
  consecutiveLostSamples_ = 0;
  proposedDirectionCount_ = 0;
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  state_ = GazeTrackerState::Locked;
  nextTrackAtMs_ = nowMs + config::GAZE_TRACK_INTERVAL_MS;
  pitchGlancePhase_ = PitchGlancePhase::None;
  pitchGlanceReadyAtMs_ = 0;
  scheduleNextPitchGlance(nowMs);
  queueHoldRequest(targetYawDeg_);
  presenceEvent_ = GazePresenceEvent::TargetConfirmed;
  if (announce) {
    Serial.printf("GAZE: target acquired yaw=%.1f distance=%u\n",
                  targetYawDeg_, targetDistanceMm_);
  }
}

void GazeTracker::loseTarget(uint32_t nowMs) {
  const bool previouslyTracked = hasTarget_;
  hasTarget_ = false;
  targetDistanceMm_ = 0;
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  pitchGlancePhase_ = PitchGlancePhase::None;
  pitchGlanceReadyAtMs_ = 0;
  nextPitchGlanceAtMs_ = 0;
  state_ = GazeTrackerState::Lost;
  nextScanAtMs_ =
      nowMs +
      (boredSearch_
           ? randomDelay(config::GAZE_BORED_RESCAN_MIN_MS,
                         config::GAZE_BORED_RESCAN_MAX_MS)
           : config::GAZE_RESCAN_INTERVAL_MS);
  queueHoldRequest(config::NECK_YAW_CENTER_DEG);
  presenceEvent_ = GazePresenceEvent::TargetLost;
  Serial.println(previouslyTracked ? "GAZE: target lost"
                                   : "GAZE: scan found no target");
}

void GazeTracker::setEnabled(bool enabled, uint32_t nowMs) {
  enabled_ = enabled;
  requestPending_ = false;
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  forceScan_ = false;
  if (!enabled) {
    state_ = GazeTrackerState::Disabled;
    return;
  }
  state_ = paused_ ? GazeTrackerState::Paused : GazeTrackerState::Idle;
  nextScanAtMs_ = nowMs;
}

void GazeTracker::setPaused(bool paused, uint32_t nowMs) {
  if (paused_ == paused || !enabled_) return;
  paused_ = paused;
  requestPending_ = false;
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  pitchGlancePhase_ = PitchGlancePhase::None;
  pitchGlanceReadyAtMs_ = 0;
  if (paused) {
    state_ = GazeTrackerState::Paused;
    Serial.println("GAZE: paused");
    return;
  }

  Serial.println("GAZE: resumed");
  if (hasTarget_ && nowMs - targetSeenAtMs_ <= config::GAZE_TARGET_MEMORY_MS) {
    state_ = GazeTrackerState::Locked;
    nextTrackAtMs_ = nowMs + config::GAZE_TRACK_INTERVAL_MS;
    scheduleNextPitchGlance(nowMs);
    queueHoldRequest(targetYawDeg_);
  } else {
    hasTarget_ = false;
    state_ = GazeTrackerState::Idle;
    nextScanAtMs_ =
        boredSearch_
            ? nowMs + randomDelay(config::GAZE_BORED_FIRST_SCAN_MIN_MS,
                                  config::GAZE_BORED_FIRST_SCAN_MAX_MS)
            : nowMs;
  }
}

void GazeTracker::setBoredSearch(bool active, uint32_t nowMs) {
  if (boredSearch_ == active) return;
  boredSearch_ = active;
  if (!active || !enabled_ || paused_ || hasTarget_) return;

  requestPending_ = false;
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  state_ = GazeTrackerState::Idle;
  forceScan_ = false;
  nextScanAtMs_ =
      nowMs + randomDelay(config::GAZE_BORED_FIRST_SCAN_MIN_MS,
                          config::GAZE_BORED_FIRST_SCAN_MAX_MS);
}

void GazeTracker::requestScan(uint32_t nowMs) {
  forceScan_ = true;
  if (enabled_ && !paused_) {
    hasTarget_ = false;
    requestPending_ = false;
    operation_ = Operation::None;
    phase_ = PointPhase::None;
    pitchGlancePhase_ = PitchGlancePhase::None;
    state_ = GazeTrackerState::Idle;
    nextScanAtMs_ = nowMs;
  }
}

void GazeTracker::releaseTarget(uint32_t nowMs) {
  hasTarget_ = false;
  targetDistanceMm_ = 0;
  requestPending_ = false;
  operation_ = Operation::None;
  phase_ = PointPhase::None;
  pitchGlancePhase_ = PitchGlancePhase::None;
  state_ = enabled_ ? (paused_ ? GazeTrackerState::Paused
                               : GazeTrackerState::Idle)
                    : GazeTrackerState::Disabled;
  nextScanAtMs_ = nowMs;
  forceScan_ = enabled_ && !paused_;
}

bool GazeTracker::takePresenceEvent(GazePresenceEvent& event) {
  if (presenceEvent_ == GazePresenceEvent::None) return false;
  event = presenceEvent_;
  presenceEvent_ = GazePresenceEvent::None;
  return true;
}

GazeMeasurementRoute GazeTracker::measurementRoute() const {
  if (!enabled_ || paused_) return GazeMeasurementRoute::Legacy;
  if (state_ == GazeTrackerState::Locked && lockedReady_ &&
      pitchGlancePhase_ == PitchGlancePhase::None)
    return GazeMeasurementRoute::TargetDirection;
  if (state_ == GazeTrackerState::Idle)
    return GazeMeasurementRoute::Ignore;
  return GazeMeasurementRoute::TrackerOnly;
}

uint32_t GazeTracker::targetAgeMs(uint32_t nowMs) const {
  return hasTarget_ ? nowMs - targetSeenAtMs_ : 0;
}

const char* GazeTracker::stateName(GazeTrackerState state) {
  switch (state) {
    case GazeTrackerState::Disabled: return "DISABLED";
    case GazeTrackerState::Idle: return "IDLE";
    case GazeTrackerState::MoveToScanPoint: return "MOVE_TO_SCAN_POINT";
    case GazeTrackerState::Settling: return "SETTLING";
    case GazeTrackerState::Sampling: return "SAMPLING";
    case GazeTrackerState::Evaluating: return "EVALUATING";
    case GazeTrackerState::Confirming: return "CONFIRMING";
    case GazeTrackerState::Locked: return "LOCKED";
    case GazeTrackerState::LocalSearch: return "LOCAL_SEARCH";
    case GazeTrackerState::Lost: return "LOST";
    case GazeTrackerState::Paused: return "PAUSED";
  }
  return "UNKNOWN";
}

void GazeTracker::printStatus(Print& output, uint32_t nowMs) const {
  output.printf(
      "GAZE_STATUS enabled=%s paused=%s state=%s target=%s yaw=%.1f distance=%u age_ms=%lu debug=%s\n",
      enabled_ ? "YES" : "NO", paused_ ? "YES" : "NO",
      stateName(state_), hasTarget_ ? "YES" : "NO", targetYawDeg_,
      targetDistanceMm_, static_cast<unsigned long>(targetAgeMs(nowMs)),
      debug_ ? "ON" : "OFF");
}

void GazeTracker::printMap(Print& output) const {
  int best = -1;
  for (size_t i = 0; i < kMaximumPoints; ++i) {
    const GazeScanPoint& point = scanMap_[i];
    output.printf("GAZE_MAP angle=%.1f actual=%.1f valid=%u/%u ",
                  point.commandedYawDeg, point.actualYawDeg,
                  point.validSamples, point.totalSamples);
    if (point.validSamples == 0) {
      output.print("median=NA candidate=NO\n");
    } else {
      output.printf("median=%u min=%u max=%u candidate=%s\n",
                    point.medianDistanceMm, point.minimumDistanceMm,
                    point.maximumDistanceMm,
                    pointIsCandidate(point) ? "YES" : "NO");
    }
    if (pointIsCandidate(point) &&
        (best < 0 || point.medianDistanceMm <
                         scanMap_[best].medianDistanceMm)) {
      best = static_cast<int>(i);
    }
  }
  if (best >= 0) {
    output.printf("GAZE_RESULT yaw=%.1f distance=%u\n",
                  scanMap_[best].commandedYawDeg,
                  scanMap_[best].medianDistanceMm);
  } else {
    output.print("GAZE_RESULT target=NONE\n");
  }
}
