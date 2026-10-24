#pragma once

#include <Arduino.h>

#include "GazeTracker.h"
#include "NeckController.h"
#include "RobotState.h"

class NeckBehaviorController {
 public:
  void begin(NeckController& neck, uint32_t nowMs);
  void update(uint32_t nowMs, RobotState state, uint32_t beatSequence,
              bool musicActive);

  void setEnabled(bool enabled, uint32_t nowMs);
  void noteManualCommand(uint32_t nowMs);
  void setGazeControlActive(bool active) { gazeControlActive_ = active; }
  void setGazeTarget(bool known, float yawDeg) {
    gazeTargetKnown_ = known;
    lastGazeYawDeg_ = yawDeg;
  }
  bool requestGaze(const GazeRequest& request, uint32_t nowMs,
                   RobotState state);

  bool enabled() const { return enabled_; }
  bool manualOverrideActive(uint32_t nowMs) const;
  uint32_t manualOverrideRemainingMs(uint32_t nowMs) const;
  const char* modeName(uint32_t nowMs) const;

 private:
  void enterState(RobotState state, uint32_t nowMs, uint32_t beatSequence);
  void updateIdle(uint32_t nowMs);
  void updateDance(uint32_t nowMs, uint32_t beatSequence, bool musicActive);
  void moveClamped(float yawDeg, float pitchDeg, float speedDegPerSec);
  void scheduleNextIdle(uint32_t nowMs);
  static float clamp(float value, float minimum, float maximum);

  NeckController* neck_ = nullptr;
  bool enabled_ = false;
  bool stateKnown_ = false;
  RobotState state_ = RobotState::Curious;
  uint32_t manualOverrideUntil_ = 0;
  uint32_t nextIdleMoveAt_ = 0;
  bool idleAwayFromCenter_ = false;
  uint32_t lastDanceStepAt_ = 0;
  uint32_t lastBeatSequence_ = 0;
  int8_t danceDirection_ = -1;
  bool danceBobDown_ = false;
  bool gazeControlActive_ = false;
  bool gazeTargetKnown_ = false;
  float lastGazeYawDeg_ = 90.0F;
};
