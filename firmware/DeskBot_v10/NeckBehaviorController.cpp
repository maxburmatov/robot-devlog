#include "NeckBehaviorController.h"

#include "Config.h"

namespace {

bool deadlineActive(uint32_t nowMs, uint32_t deadlineMs) {
  return deadlineMs != 0 && static_cast<int32_t>(deadlineMs - nowMs) > 0;
}

}  // namespace

void NeckBehaviorController::begin(NeckController& neck, uint32_t nowMs) {
  neck_ = &neck;
  enabled_ = config::NECK_BEHAVIOR_ENABLED_BY_DEFAULT;
  stateKnown_ = false;
  manualOverrideUntil_ = 0;
  scheduleNextIdle(nowMs);
}

void NeckBehaviorController::setEnabled(bool enabled, uint32_t nowMs) {
  enabled_ = enabled;
  manualOverrideUntil_ = 0;
  stateKnown_ = false;
  idleAwayFromCenter_ = false;
  scheduleNextIdle(nowMs);
}

void NeckBehaviorController::noteManualCommand(uint32_t nowMs) {
  if (!enabled_) return;
  manualOverrideUntil_ = nowMs + config::NECK_MANUAL_OVERRIDE_MS;
  stateKnown_ = false;
}

bool NeckBehaviorController::requestGaze(const GazeRequest& request,
                                         uint32_t nowMs,
                                         RobotState state) {
  if (!enabled_ || neck_ == nullptr || !neck_->isAvailable() ||
      manualOverrideActive(nowMs) || !request.hasTarget) {
    return false;
  }
  if (state != RobotState::Curious && state != RobotState::Waiting &&
      state != RobotState::Bored && state != RobotState::Happy &&
      state != RobotState::Content) {
    return false;
  }
  moveClamped(request.yawDeg, request.pitchDeg, request.speedDegPerSec);
  gazeTargetKnown_ = true;
  lastGazeYawDeg_ = request.yawDeg;
  return true;
}

bool NeckBehaviorController::manualOverrideActive(uint32_t nowMs) const {
  return deadlineActive(nowMs, manualOverrideUntil_);
}

uint32_t NeckBehaviorController::manualOverrideRemainingMs(
    uint32_t nowMs) const {
  return manualOverrideActive(nowMs) ? manualOverrideUntil_ - nowMs : 0;
}

const char* NeckBehaviorController::modeName(uint32_t nowMs) const {
  if (!enabled_) return "OFF";
  if (manualOverrideActive(nowMs)) return "MANUAL_OVERRIDE";
  return "AUTO";
}

float NeckBehaviorController::clamp(float value, float minimum,
                                    float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

void NeckBehaviorController::moveClamped(float yawDeg, float pitchDeg,
                                         float speedDegPerSec) {
  if (neck_ == nullptr || !neck_->isAvailable()) return;
  neck_->moveTo(clamp(yawDeg, config::NECK_YAW_MIN_DEG,
                      config::NECK_YAW_MAX_DEG),
                clamp(pitchDeg, config::NECK_PITCH_MIN_DEG,
                      config::NECK_PITCH_MAX_DEG),
                speedDegPerSec);
}

void NeckBehaviorController::scheduleNextIdle(uint32_t nowMs) {
  const uint32_t interval = static_cast<uint32_t>(random(
      static_cast<long>(config::NECK_IDLE_MIN_INTERVAL_MS),
      static_cast<long>(config::NECK_IDLE_MAX_INTERVAL_MS + 1)));
  nextIdleMoveAt_ = nowMs + interval;
}

void NeckBehaviorController::enterState(RobotState state, uint32_t nowMs,
                                        uint32_t beatSequence) {
  idleAwayFromCenter_ = false;
  lastDanceStepAt_ = nowMs;
  lastBeatSequence_ = beatSequence;
  danceDirection_ = -1;
  danceBobDown_ = false;

  switch (state) {
    case RobotState::Curious:
    case RobotState::Waiting:
    case RobotState::Bored:
    case RobotState::Happy:
    case RobotState::Content:
      if (gazeControlActive_) {
        scheduleNextIdle(nowMs);
        break;
      }
      moveClamped(config::NECK_YAW_CENTER_DEG,
                  config::NECK_PITCH_CENTER_DEG,
                  config::NECK_IDLE_SPEED_DEG_PER_SEC);
      scheduleNextIdle(nowMs);
      break;

    case RobotState::Wake:
      moveClamped(config::NECK_YAW_CENTER_DEG,
                  config::NECK_PITCH_CENTER_DEG +
                      config::NECK_WAKE_PITCH_OFFSET_DEG,
                  config::NECK_REACTION_SPEED_DEG_PER_SEC);
      break;

    case RobotState::Drowsy:
    case RobotState::Sleep:
      moveClamped(config::NECK_YAW_CENTER_DEG,
                  config::NECK_PITCH_MIN_DEG,
                  config::NECK_POSE_SPEED_DEG_PER_SEC);
      break;

    case RobotState::Attention:
      moveClamped(gazeTargetKnown_ ? lastGazeYawDeg_
                                   : config::NECK_YAW_CENTER_DEG,
                  config::NECK_PITCH_CENTER_DEG +
                      config::NECK_ATTENTION_PITCH_OFFSET_DEG,
                  config::NECK_REACTION_SPEED_DEG_PER_SEC);
      break;

    case RobotState::Greeting:
      if (neck_ != nullptr && neck_->isAvailable()) {
        neck_->startGesture(NeckGesture::Nod);
      }
      break;

    case RobotState::Dancing:
      moveClamped(config::NECK_YAW_CENTER_DEG,
                  config::NECK_PITCH_CENTER_DEG,
                  config::NECK_DANCE_SPEED_DEG_PER_SEC);
      // Permit the first manual-dance step immediately after centering starts.
      lastDanceStepAt_ = nowMs - config::NECK_MANUAL_DANCE_STEP_MS;
      break;

    default:
      // Temporary reactions end in a known neutral pose. PickedUp and
      // UpsideDown intentionally have no dedicated stop/safety behavior in
      // this version because the IMU is optional and not installed.
      moveClamped(config::NECK_YAW_CENTER_DEG,
                  config::NECK_PITCH_CENTER_DEG,
                  config::NECK_POSE_SPEED_DEG_PER_SEC);
      break;
  }
}

void NeckBehaviorController::updateIdle(uint32_t nowMs) {
  if (neck_ == nullptr || neck_->isMoving() ||
      static_cast<int32_t>(nowMs - nextIdleMoveAt_) < 0) {
    return;
  }

  if (idleAwayFromCenter_) {
    moveClamped(config::NECK_YAW_CENTER_DEG,
                config::NECK_PITCH_CENTER_DEG,
                config::NECK_IDLE_SPEED_DEG_PER_SEC);
    idleAwayFromCenter_ = false;
    scheduleNextIdle(nowMs);
    return;
  }

  const float yawOffset =
      random(-1000, 1001) * config::NECK_IDLE_YAW_AMPLITUDE_DEG / 1000.0F;
  const float pitchOffset =
      random(-1000, 1001) * config::NECK_IDLE_PITCH_AMPLITUDE_DEG / 1000.0F;
  moveClamped(config::NECK_YAW_CENTER_DEG + yawOffset,
              config::NECK_PITCH_CENTER_DEG + pitchOffset,
              config::NECK_IDLE_SPEED_DEG_PER_SEC);
  idleAwayFromCenter_ = true;
  nextIdleMoveAt_ = nowMs + config::NECK_IDLE_RETURN_DELAY_MS;
}

void NeckBehaviorController::updateDance(uint32_t nowMs,
                                         uint32_t beatSequence,
                                         bool musicActive) {
  const bool newBeat = musicActive && beatSequence != lastBeatSequence_;
  const bool manualStep =
      !musicActive &&
      nowMs - lastDanceStepAt_ >= config::NECK_MANUAL_DANCE_STEP_MS;
  if (!newBeat && !manualStep) return;

  lastBeatSequence_ = beatSequence;
  lastDanceStepAt_ = nowMs;
  danceDirection_ = -danceDirection_;
  danceBobDown_ = !danceBobDown_;

  const float yaw = config::NECK_YAW_CENTER_DEG +
                    danceDirection_ * config::NECK_DANCE_YAW_AMPLITUDE_DEG;
  const float pitch = config::NECK_PITCH_CENTER_DEG -
                      (danceBobDown_
                           ? config::NECK_DANCE_PITCH_AMPLITUDE_DEG
                           : 0.0F);
  moveClamped(yaw, pitch, config::NECK_DANCE_SPEED_DEG_PER_SEC);
}

void NeckBehaviorController::update(uint32_t nowMs, RobotState state,
                                    uint32_t beatSequence,
                                    bool musicActive) {
  if (!enabled_ || neck_ == nullptr || !neck_->isAvailable()) return;

  if (manualOverrideActive(nowMs)) return;
  if (manualOverrideUntil_ != 0) {
    manualOverrideUntil_ = 0;
    stateKnown_ = false;
  }

  if (!stateKnown_ || state != state_) {
    state_ = state;
    stateKnown_ = true;
    enterState(state, nowMs, beatSequence);
  }

  if ((state_ == RobotState::Curious || state_ == RobotState::Waiting ||
       state_ == RobotState::Bored || state_ == RobotState::Happy ||
       state_ == RobotState::Content) &&
      !gazeControlActive_) {
    updateIdle(nowMs);
  } else if (state_ == RobotState::Dancing) {
    updateDance(nowMs, beatSequence, musicActive);
  }
}
