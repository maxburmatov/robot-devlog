#include "NeckController.h"

#include <cmath>

#include "Config.h"

namespace {

float clampValue(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

}  // namespace

bool NeckController::begin() {
  currentYawDeg_ = config::NECK_YAW_CENTER_DEG;
  currentPitchDeg_ = config::NECK_PITCH_CENTER_DEG;
  startYawDeg_ = currentYawDeg_;
  startPitchDeg_ = currentPitchDeg_;
  targetYawDeg_ = currentYawDeg_;
  targetPitchDeg_ = currentPitchDeg_;

  if (!config::NECK_ENABLED) {
    lastError_ = "DISABLED";
    publishSnapshot();
    return false;
  }

  available_ = attachOutputs();
  if (!available_) {
    publishSnapshot();
    return false;
  }

  writePosition(currentYawDeg_, currentPitchDeg_);
  idleSince_ = millis();

  commandQueue_ = xQueueCreate(1, sizeof(Command));
  if (commandQueue_ == nullptr) {
    lastError_ = "COMMAND_QUEUE_CREATE_FAILED";
    detachInternal();
    available_ = false;
    publishSnapshot();
    return false;
  }

  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "deskbot-neck", config::NECK_TASK_STACK_BYTES, this,
      config::NECK_TASK_PRIORITY, &taskHandle_, config::NECK_TASK_CORE);
  if (created != pdPASS) {
    lastError_ = "TASK_CREATE_FAILED";
    vQueueDelete(commandQueue_);
    commandQueue_ = nullptr;
    detachInternal();
    available_ = false;
    publishSnapshot();
    return false;
  }

  publishSnapshot();
  return true;
}

void NeckController::taskEntry(void* context) {
  static_cast<NeckController*>(context)->task();
}

void NeckController::task() {
  TickType_t nextWake = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(config::NECK_UPDATE_INTERVAL_MS);

  for (;;) {
    Command command;
    if (xQueueReceive(commandQueue_, &command, 0) == pdTRUE) {
      applyCommand(command);
    }
    updateMotion(millis());
    publishSnapshot();
    vTaskDelayUntil(&nextWake, interval);
  }
}

bool NeckController::enqueue(const Command& command) {
  if (commandQueue_ == nullptr || !isAvailable()) return false;
  return xQueueOverwrite(commandQueue_, &command) == pdPASS;
}

void NeckController::applyCommand(const Command& command) {
  switch (command.type) {
    case CommandType::Move:
      startMoveInternal(command.yawDeg, command.pitchDeg,
                        command.speedDegPerSec, true);
      break;
    case CommandType::Stop:
      stopInternal();
      break;
    case CommandType::Detach:
      detachInternal();
      break;
    case CommandType::Gesture:
      startGestureInternal(command.gesture);
      break;
  }
}

void NeckController::publishSnapshot() {
  portENTER_CRITICAL(&snapshotLock_);
  snapshot_.available = available_;
  snapshot_.attached = attached_;
  snapshot_.moving = moving_;
  snapshot_.yawDeg = currentYawDeg_;
  snapshot_.pitchDeg = currentPitchDeg_;
  snapshot_.targetYawDeg = targetYawDeg_;
  snapshot_.targetPitchDeg = targetPitchDeg_;
  snapshot_.gesture = gesture_;
  snapshot_.lastError = lastError_;
  portEXIT_CRITICAL(&snapshotLock_);
}

bool NeckController::attachOutputs() {
  if (attached_) return true;

  if (!ledcAttach(config::NECK_YAW_PIN, config::NECK_PWM_FREQUENCY_HZ,
                  config::NECK_PWM_RESOLUTION_BITS)) {
    lastError_ = "YAW_PWM_ATTACH_FAILED";
    return false;
  }
  if (!ledcAttach(config::NECK_PITCH_PIN, config::NECK_PWM_FREQUENCY_HZ,
                  config::NECK_PWM_RESOLUTION_BITS)) {
    ledcDetach(config::NECK_YAW_PIN);
    lastError_ = "PITCH_PWM_ATTACH_FAILED";
    return false;
  }

  attached_ = true;
  available_ = true;
  lastError_ = "NONE";
  return true;
}

void NeckController::writeAxis(uint8_t pin, float angleDeg, bool inverted,
                               uint16_t minimumPulseUs,
                               uint16_t maximumPulseUs) {
  float physicalAngle = clampValue(angleDeg, 0.0F, 180.0F);
  if (inverted) physicalAngle = 180.0F - physicalAngle;

  const float pulseUs =
      minimumPulseUs + (maximumPulseUs - minimumPulseUs) *
                           (physicalAngle / 180.0F);
  const uint32_t periodUs = 1000000UL / config::NECK_PWM_FREQUENCY_HZ;
  const uint32_t maximumDuty =
      (1UL << config::NECK_PWM_RESOLUTION_BITS) - 1UL;
  const uint32_t duty = static_cast<uint32_t>(
      std::lround(pulseUs * static_cast<float>(maximumDuty) / periodUs));
  ledcWrite(pin, duty);
}

void NeckController::writePosition(float yawDeg, float pitchDeg) {
  if (!attached_) return;
  writeAxis(config::NECK_YAW_PIN, yawDeg, config::NECK_YAW_INVERTED,
            config::NECK_YAW_MIN_PULSE_US, config::NECK_YAW_MAX_PULSE_US);
  writeAxis(config::NECK_PITCH_PIN, pitchDeg, config::NECK_PITCH_INVERTED,
            config::NECK_PITCH_MIN_PULSE_US,
            config::NECK_PITCH_MAX_PULSE_US);
}

bool NeckController::startMoveInternal(float yawDeg, float pitchDeg,
                                       float speedDegPerSec,
                                       bool cancelGesture) {
  if (!available_ || !std::isfinite(yawDeg) || !std::isfinite(pitchDeg) ||
      !std::isfinite(speedDegPerSec) || speedDegPerSec <= 0.0F) {
    return false;
  }
  if (!attachOutputs()) return false;

  if (cancelGesture) {
    gesture_ = NeckGesture::None;
    gestureStep_ = 0;
  }

  startYawDeg_ = currentYawDeg_;
  startPitchDeg_ = currentPitchDeg_;
  targetYawDeg_ = clampValue(yawDeg, config::NECK_YAW_MIN_DEG,
                             config::NECK_YAW_MAX_DEG);
  targetPitchDeg_ = clampValue(pitchDeg, config::NECK_PITCH_MIN_DEG,
                               config::NECK_PITCH_MAX_DEG);

  const float maximumDistance =
      fmaxf(fabsf(targetYawDeg_ - startYawDeg_),
            fabsf(targetPitchDeg_ - startPitchDeg_));
  if (maximumDistance < 0.01F) {
    currentYawDeg_ = targetYawDeg_;
    currentPitchDeg_ = targetPitchDeg_;
    writePosition(currentYawDeg_, currentPitchDeg_);
    moving_ = false;
    idleSince_ = millis();
    return true;
  }

  moveStartedAt_ = millis();
  moveDurationMs_ = static_cast<uint32_t>(
      ceilf(maximumDistance * 1000.0F / speedDegPerSec));
  if (moveDurationMs_ < config::NECK_UPDATE_INTERVAL_MS) {
    moveDurationMs_ = config::NECK_UPDATE_INTERVAL_MS;
  }
  moving_ = true;
  return true;
}

bool NeckController::moveTo(float yawDeg, float pitchDeg,
                            float speedDegPerSec) {
  if (!std::isfinite(yawDeg) || !std::isfinite(pitchDeg) ||
      !std::isfinite(speedDegPerSec) || speedDegPerSec <= 0.0F) {
    return false;
  }
  const Command command = {CommandType::Move, yawDeg, pitchDeg,
                           speedDegPerSec, NeckGesture::None};
  return enqueue(command);
}

bool NeckController::center(float speedDegPerSec) {
  return moveTo(config::NECK_YAW_CENTER_DEG,
                config::NECK_PITCH_CENTER_DEG, speedDegPerSec);
}

void NeckController::stop() {
  enqueue({CommandType::Stop});
}

void NeckController::stopInternal() {
  gesture_ = NeckGesture::None;
  gestureStep_ = 0;
  targetYawDeg_ = currentYawDeg_;
  targetPitchDeg_ = currentPitchDeg_;
  moving_ = false;
  idleSince_ = millis();
  writePosition(currentYawDeg_, currentPitchDeg_);
}

void NeckController::detach() {
  enqueue({CommandType::Detach});
}

void NeckController::detachInternal() {
  if (attached_) {
    ledcDetach(config::NECK_YAW_PIN);
    ledcDetach(config::NECK_PITCH_PIN);
  }
  attached_ = false;
  moving_ = false;
  gesture_ = NeckGesture::None;
  gestureStep_ = 0;
}

bool NeckController::startGesture(NeckGesture gesture) {
  if (gesture == NeckGesture::None) return false;
  Command command;
  command.type = CommandType::Gesture;
  command.gesture = gesture;
  return enqueue(command);
}

bool NeckController::startGestureInternal(NeckGesture gesture) {
  if (!available_ || gesture == NeckGesture::None) return false;
  gesture_ = gesture;
  gestureStep_ = 0;
  advanceGesture();
  return true;
}

void NeckController::advanceGesture() {
  const float speed = config::NECK_DEFAULT_SPEED_DEG_PER_SEC;

  if (gesture_ == NeckGesture::Nod) {
    switch (gestureStep_++) {
      case 0:
        startMoveInternal(config::NECK_YAW_CENTER_DEG,
                          config::NECK_PITCH_CENTER_DEG, speed, false);
        return;
      case 1:
        startMoveInternal(config::NECK_YAW_CENTER_DEG,
                          config::NECK_PITCH_MIN_DEG, speed, false);
        return;
      case 2:
        startMoveInternal(config::NECK_YAW_CENTER_DEG,
                          config::NECK_PITCH_CENTER_DEG, speed, false);
        return;
      default:
        break;
    }
  } else if (gesture_ == NeckGesture::LookAround) {
    switch (gestureStep_++) {
      case 0:
        startMoveInternal(config::NECK_YAW_CENTER_DEG,
                          config::NECK_PITCH_CENTER_DEG, speed, false);
        return;
      case 1:
        startMoveInternal(config::NECK_YAW_MIN_DEG,
                          config::NECK_PITCH_CENTER_DEG, speed, false);
        return;
      case 2:
        startMoveInternal(config::NECK_YAW_CENTER_DEG,
                          config::NECK_PITCH_CENTER_DEG, speed, false);
        return;
      case 3:
        startMoveInternal(config::NECK_YAW_MAX_DEG,
                          config::NECK_PITCH_CENTER_DEG, speed, false);
        return;
      case 4:
        startMoveInternal(config::NECK_YAW_CENTER_DEG,
                          config::NECK_PITCH_CENTER_DEG, speed, false);
        return;
      default:
        break;
    }
  }

  gesture_ = NeckGesture::None;
  gestureStep_ = 0;
}

void NeckController::updateMotion(uint32_t nowMs) {
  if (!available_) return;

  if (moving_) {
    if (nowMs - lastUpdateAt_ < config::NECK_UPDATE_INTERVAL_MS) return;
    lastUpdateAt_ = nowMs;

    const uint32_t elapsedMs = nowMs - moveStartedAt_;
    float progress = moveDurationMs_ == 0
                         ? 1.0F
                         : static_cast<float>(elapsedMs) / moveDurationMs_;
    if (progress > 1.0F) progress = 1.0F;

    // Smoothstep avoids abrupt changes of speed at both ends.
    const float eased = progress * progress * (3.0F - 2.0F * progress);
    currentYawDeg_ =
        startYawDeg_ + (targetYawDeg_ - startYawDeg_) * eased;
    currentPitchDeg_ =
        startPitchDeg_ + (targetPitchDeg_ - startPitchDeg_) * eased;
    writePosition(currentYawDeg_, currentPitchDeg_);

    if (progress >= 1.0F) {
      moving_ = false;
      idleSince_ = nowMs;
      if (gesture_ != NeckGesture::None) advanceGesture();
    }
    return;
  }

  if (gesture_ != NeckGesture::None) {
    advanceGesture();
    return;
  }

  if (attached_ && config::NECK_HOLD_TIMEOUT_MS > 0 &&
      nowMs - idleSince_ >= config::NECK_HOLD_TIMEOUT_MS) {
    detachInternal();
  }
}

bool NeckController::isAvailable() const {
  portENTER_CRITICAL(&snapshotLock_);
  const bool value = snapshot_.available;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

bool NeckController::isAttached() const {
  portENTER_CRITICAL(&snapshotLock_);
  const bool value = snapshot_.attached;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

bool NeckController::isMoving() const {
  portENTER_CRITICAL(&snapshotLock_);
  const bool value = snapshot_.moving;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

float NeckController::yaw() const {
  portENTER_CRITICAL(&snapshotLock_);
  const float value = snapshot_.yawDeg;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

float NeckController::pitch() const {
  portENTER_CRITICAL(&snapshotLock_);
  const float value = snapshot_.pitchDeg;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

float NeckController::targetYaw() const {
  portENTER_CRITICAL(&snapshotLock_);
  const float value = snapshot_.targetYawDeg;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

float NeckController::targetPitch() const {
  portENTER_CRITICAL(&snapshotLock_);
  const float value = snapshot_.targetPitchDeg;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

NeckGesture NeckController::gesture() const {
  portENTER_CRITICAL(&snapshotLock_);
  const NeckGesture value = snapshot_.gesture;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

const char* NeckController::lastError() const {
  portENTER_CRITICAL(&snapshotLock_);
  const char* value = snapshot_.lastError;
  portEXIT_CRITICAL(&snapshotLock_);
  return value;
}

const char* NeckController::gestureName(NeckGesture gesture) {
  switch (gesture) {
    case NeckGesture::Nod:
      return "NOD";
    case NeckGesture::LookAround:
      return "LOOK";
    case NeckGesture::None:
    default:
      return "NONE";
  }
}
