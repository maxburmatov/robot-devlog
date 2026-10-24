#pragma once

#include <Arduino.h>

#include "Config.h"

enum class GazeTrackerState : uint8_t {
  Disabled,
  Idle,
  MoveToScanPoint,
  Settling,
  Sampling,
  Evaluating,
  Confirming,
  Locked,
  LocalSearch,
  Lost,
  Paused,
};

struct GazeMeasurement {
  uint32_t timestampMs = 0;
  uint16_t distanceMm = 0;
  uint8_t rangeStatus = 255;
  bool valid = false;
};

struct GazeRequest {
  bool hasTarget = false;
  float yawDeg = config::NECK_YAW_CENTER_DEG;
  float pitchDeg = config::NECK_PITCH_CENTER_DEG;
  float speedDegPerSec = config::GAZE_SCAN_SPEED_DEG_PER_SEC;
};

struct GazeScanPoint {
  float commandedYawDeg = 0.0F;
  float actualYawDeg = 0.0F;
  uint16_t medianDistanceMm = 0;
  uint16_t minimumDistanceMm = 0;
  uint16_t maximumDistanceMm = 0;
  uint8_t validSamples = 0;
  uint8_t totalSamples = 0;
  uint32_t measuredAtMs = 0;
};

enum class GazePresenceEvent : uint8_t {
  None,
  TargetConfirmed,
  TargetLost,
};

enum class GazeMeasurementRoute : uint8_t {
  Legacy,
  TrackerOnly,
  TargetDirection,
  Ignore,
};

class GazeTracker {
 public:
  void begin(uint32_t nowMs);
  void update(uint32_t nowMs, float currentYaw, bool neckMoving);
  void onDistanceMeasurement(const GazeMeasurement& measurement);
  bool takeRequest(GazeRequest& request);
  void setEnabled(bool enabled, uint32_t nowMs);
  void setPaused(bool paused, uint32_t nowMs);
  void setBoredSearch(bool active, uint32_t nowMs);
  void requestScan(uint32_t nowMs);
  void releaseTarget(uint32_t nowMs);

  bool takePresenceEvent(GazePresenceEvent& event);
  GazeMeasurementRoute measurementRoute() const;
  GazeTrackerState state() const { return state_; }
  bool enabled() const { return enabled_; }
  bool paused() const { return paused_; }
  bool hasTarget() const { return hasTarget_; }
  float targetYaw() const { return targetYawDeg_; }
  uint16_t targetDistanceMm() const { return targetDistanceMm_; }
  uint32_t targetAgeMs(uint32_t nowMs) const;
  void setDebug(bool enabled) { debug_ = enabled; }
  bool debug() const { return debug_; }
  void printStatus(Print& output, uint32_t nowMs) const;
  void printMap(Print& output) const;
  static const char* stateName(GazeTrackerState state);

 private:
  enum class Operation : uint8_t {
    None,
    FullScan,
    Confirmation,
    Tracking,
    Reacquire,
  };

  enum class PointPhase : uint8_t {
    None,
    WaitingForRequest,
    WaitingForArrival,
    Settling,
    Sampling,
  };

  enum class PitchGlancePhase : uint8_t {
    None,
    Away,
    Returning,
  };

  static constexpr size_t kMaximumPoints = config::GAZE_SCAN_POINT_COUNT;

  void startFullScan(uint32_t nowMs);
  void startOperation(Operation operation, const float* angles, size_t count,
                      uint32_t nowMs);
  void queueCurrentPoint();
  void finishCurrentPoint(uint32_t nowMs);
  void evaluateFullScan(uint32_t nowMs, float currentYaw);
  void evaluateLocalSearch(uint32_t nowMs);
  void lockTarget(float yawDeg, uint16_t distanceMm, uint32_t nowMs,
                  bool announce);
  void loseTarget(uint32_t nowMs);
  void queuePoseRequest(float yawDeg, float pitchDeg, float speedDegPerSec);
  void queueHoldRequest(float yawDeg);
  void startPitchGlance(uint32_t nowMs);
  void scheduleNextPitchGlance(uint32_t nowMs);
  uint32_t randomDelay(uint32_t minimumMs, uint32_t maximumMs) const;
  void resetPoint(GazeScanPoint& point, float yawDeg);
  bool pointIsCandidate(const GazeScanPoint& point) const;
  int selectBestPoint(const GazeScanPoint* points, size_t count,
                      float referenceYaw) const;
  uint16_t medianOfValidSamples() const;
  float correctedYaw(float yawDeg) const;
  void setPublicStateForPhase();

  bool enabled_ = false;
  bool paused_ = false;
  bool debug_ = false;
  bool reverseScan_ = false;
  bool forceScan_ = false;
  bool boredSearch_ = false;
  bool pitchGlanceLooksUp_ = false;
  bool hasTarget_ = false;
  bool requestPending_ = false;
  bool requestTaken_ = false;
  bool lockedReady_ = false;
  GazeTrackerState state_ = GazeTrackerState::Disabled;
  Operation operation_ = Operation::None;
  PointPhase phase_ = PointPhase::None;
  PitchGlancePhase pitchGlancePhase_ = PitchGlancePhase::None;
  GazeRequest pendingRequest_;
  GazePresenceEvent presenceEvent_ = GazePresenceEvent::None;

  GazeScanPoint workPoints_[kMaximumPoints] = {};
  GazeScanPoint scanMap_[kMaximumPoints] = {};
  float operationAngles_[kMaximumPoints] = {};
  uint16_t validSamples_[config::GAZE_SAMPLES_PER_POINT] = {};
  size_t operationPointCount_ = 0;
  size_t operationPointIndex_ = 0;
  uint8_t sampleAttempts_ = 0;
  uint8_t validSampleCount_ = 0;
  uint8_t consecutiveLostSamples_ = 0;
  uint8_t proposedDirectionCount_ = 0;

  float currentYawDeg_ = config::NECK_YAW_CENTER_DEG;
  float targetYawDeg_ = config::NECK_YAW_CENTER_DEG;
  float candidateYawDeg_ = config::NECK_YAW_CENTER_DEG;
  float proposedYawDeg_ = config::NECK_YAW_CENTER_DEG;
  uint16_t targetDistanceMm_ = 0;
  uint32_t targetSeenAtMs_ = 0;
  uint32_t settleStartedAtMs_ = 0;
  uint32_t samplingStartedAtMs_ = 0;
  uint32_t lockedStableSinceMs_ = 0;
  uint32_t pitchGlanceReadyAtMs_ = 0;
  uint32_t nextPitchGlanceAtMs_ = 0;
  uint32_t nextScanAtMs_ = 0;
  uint32_t nextTrackAtMs_ = 0;
};
