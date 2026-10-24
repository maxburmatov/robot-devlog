#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

enum class NeckGesture : uint8_t {
  None,
  Nod,
  LookAround,
};

class NeckController {
 public:
  bool begin();

  bool moveTo(float yawDeg, float pitchDeg, float speedDegPerSec);
  bool center(float speedDegPerSec);
  void stop();
  void detach();

  bool startGesture(NeckGesture gesture);
  bool isAvailable() const;
  bool isAttached() const;
  bool isMoving() const;
  float yaw() const;
  float pitch() const;
  float targetYaw() const;
  float targetPitch() const;
  NeckGesture gesture() const;
  const char* lastError() const;
  static const char* gestureName(NeckGesture gesture);

 private:
  enum class CommandType : uint8_t { Move, Stop, Detach, Gesture };

  struct Command {
    CommandType type = CommandType::Stop;
    float yawDeg = 0.0F;
    float pitchDeg = 0.0F;
    float speedDegPerSec = 0.0F;
    NeckGesture gesture = NeckGesture::None;
  };

  struct Snapshot {
    bool available = false;
    bool attached = false;
    bool moving = false;
    float yawDeg = 90.0F;
    float pitchDeg = 90.0F;
    float targetYawDeg = 90.0F;
    float targetPitchDeg = 90.0F;
    NeckGesture gesture = NeckGesture::None;
    const char* lastError = "NONE";
  };

  static void taskEntry(void* context);
  void task();
  bool enqueue(const Command& command);
  void applyCommand(const Command& command);
  void publishSnapshot();

  bool attachOutputs();
  bool startMoveInternal(float yawDeg, float pitchDeg, float speedDegPerSec,
                         bool cancelGesture);
  void stopInternal();
  void detachInternal();
  bool startGestureInternal(NeckGesture gesture);
  void updateMotion(uint32_t nowMs);
  void writePosition(float yawDeg, float pitchDeg);
  void writeAxis(uint8_t pin, float angleDeg, bool inverted,
                 uint16_t minimumPulseUs, uint16_t maximumPulseUs);
  void advanceGesture();

  bool available_ = false;
  bool attached_ = false;
  bool moving_ = false;
  float currentYawDeg_ = 90.0F;
  float currentPitchDeg_ = 90.0F;
  float startYawDeg_ = 90.0F;
  float startPitchDeg_ = 90.0F;
  float targetYawDeg_ = 90.0F;
  float targetPitchDeg_ = 90.0F;
  uint32_t moveStartedAt_ = 0;
  uint32_t moveDurationMs_ = 0;
  uint32_t lastUpdateAt_ = 0;
  uint32_t idleSince_ = 0;
  NeckGesture gesture_ = NeckGesture::None;
  uint8_t gestureStep_ = 0;
  const char* lastError_ = "NONE";

  QueueHandle_t commandQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  mutable portMUX_TYPE snapshotLock_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot snapshot_;
};
