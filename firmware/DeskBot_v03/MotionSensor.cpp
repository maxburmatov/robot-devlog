#include "MotionSensor.h"

#include <math.h>

#include "Config.h"

namespace {

float vectorLength(float x, float y, float z) {
  return sqrtf(x * x + y * y + z * z);
}

}  // namespace

MotionSensor::MotionSensor()
    : imu_(I2C_MODE, config::IMU_ADDRESS) {}

bool MotionSensor::begin() {
  // 104 Hz is sufficient for gestures and reduces traffic on the shared bus.
  imu_.settings.accelRange = 4;
  imu_.settings.accelSampleRate = 104;
  imu_.settings.accelBandWidth = 50;
  imu_.settings.accelFifoEnabled = 0;
  imu_.settings.gyroRange = 500;
  imu_.settings.gyroSampleRate = 104;
  imu_.settings.gyroBandWidth = 50;
  imu_.settings.gyroFifoEnabled = 0;

  available_ = (imu_.begin() == IMU_SUCCESS);
  if (!available_) {
    return false;
  }

  delay(100);
  calibrate();
  lastMotionAt_ = millis();
  lastPickupAt_ = millis() - config::IMU_PICKUP_COOLDOWN_MS;
  lastShakeAt_ = millis();
  return true;
}

void MotionSensor::calibrate() {
  constexpr int SAMPLE_COUNT = 50;
  float sumX = 0.0F;
  float sumY = 0.0F;
  float sumZ = 0.0F;

  for (int sample = 0; sample < SAMPLE_COUNT; ++sample) {
    sumX += imu_.readFloatAccelX();
    sumY += imu_.readFloatAccelY();
    sumZ += imu_.readFloatAccelZ();
    delay(10);
  }

  const float averageX = sumX / SAMPLE_COUNT;
  const float averageY = sumY / SAMPLE_COUNT;
  const float averageZ = sumZ / SAMPLE_COUNT;
  const float magnitude = vectorLength(averageX, averageY, averageZ);

  if (magnitude > 0.2F) {
    referenceX_ = averageX / magnitude;
    referenceY_ = averageY / magnitude;
    referenceZ_ = averageZ / magnitude;
  }

  filteredX_ = averageX;
  filteredY_ = averageY;
  filteredZ_ = averageZ;
}

bool MotionSensor::update(unsigned long now, MotionReading& reading) {
  if (!available_) {
    return false;
  }

  const float accelX = imu_.readFloatAccelX();
  const float accelY = imu_.readFloatAccelY();
  const float accelZ = imu_.readFloatAccelZ();
  const float gyroX = imu_.readFloatGyroX();
  const float gyroY = imu_.readFloatGyroY();
  const float gyroZ = imu_.readFloatGyroZ();

  constexpr float FILTER_ALPHA = 0.25F;
  filteredX_ += FILTER_ALPHA * (accelX - filteredX_);
  filteredY_ += FILTER_ALPHA * (accelY - filteredY_);
  filteredZ_ += FILTER_ALPHA * (accelZ - filteredZ_);

  const float accelMagnitude = vectorLength(accelX, accelY, accelZ);
  const float filteredMagnitude =
      vectorLength(filteredX_, filteredY_, filteredZ_);
  const float gyroMagnitude = vectorLength(gyroX, gyroY, gyroZ);
  const float accelerationDelta = fabsf(accelMagnitude - 1.0F);

  float orientationDot = 1.0F;
  if (filteredMagnitude > 0.2F) {
    orientationDot =
        (filteredX_ * referenceX_ +
         filteredY_ * referenceY_ +
         filteredZ_ * referenceZ_) /
        filteredMagnitude;
  }

  const bool moving =
      accelerationDelta >= config::IMU_MOVE_ACCEL_DELTA_G ||
      gyroMagnitude >= config::IMU_MOVE_GYRO_DPS;
  const bool restedLongEnough =
      now - lastMotionAt_ >= config::IMU_REST_BEFORE_MOVE_MS;
  const bool rawMovementStarted = moving && restedLongEnough;
  if (moving) {
    lastMotionAt_ = now;
  }

  const bool strongMotion =
      accelerationDelta >= config::IMU_SHAKE_ACCEL_DELTA_G ||
      gyroMagnitude >= config::IMU_SHAKE_GYRO_DPS;
  const bool shaken =
      strongMotion && now - lastShakeAt_ >= config::IMU_SHAKE_COOLDOWN_MS;
  if (shaken) {
    lastShakeAt_ = now;
  }

  bool movementStarted = false;
  bool pickedUp = false;
  bool putDown = false;

  if (rawMovementStarted && !strongMotion) {
    pickupCandidate_ = true;
    pickupCandidateStartedAt_ = now;
  }

  if (pickupCandidate_) {
    const bool motionIsRecent =
        now - lastMotionAt_ <= config::IMU_PICKUP_MOTION_GAP_MS;
    const bool pickupConfirmed =
        lastMotionAt_ - pickupCandidateStartedAt_ >=
        config::IMU_PICKUP_CONFIRM_MS;

    if (strongMotion) {
      // Shake имеет собственную реакцию и не должен выглядеть как поднятие.
      pickupCandidate_ = false;
    } else if (pickupConfirmed && motionIsRecent) {
      if (waitingForPutDown_) {
        // Первое отдельное движение после pickup считаем постановкой. Оно
        // завершает pickup episode и не превращается в обычный Surprised.
        putDown = true;
        waitingForPutDown_ = false;
      } else if (now - lastPickupAt_ >= config::IMU_PICKUP_COOLDOWN_MS) {
        pickedUp = true;
        waitingForPutDown_ = true;
        lastPickupAt_ = now;
      } else {
        movementStarted = true;
      }
      pickupCandidate_ = false;
    } else if (!motionIsRecent) {
      // Короткое движение закончилось до подтверждения поднятия.
      if (waitingForPutDown_) {
        putDown = true;
        waitingForPutDown_ = false;
      } else {
        movementStarted = true;
      }
      pickupCandidate_ = false;
    }
  }

  const bool wasUpsideDown = upsideDown_;
  if (!upsideDown_ && orientationDot <= config::IMU_UPSIDE_DOWN_DOT) {
    upsideDown_ = true;
  } else if (upsideDown_ && orientationDot >= config::IMU_UPRIGHT_DOT) {
    upsideDown_ = false;
  }

  const float sidewaysTilt = filteredX_ - referenceX_;
  int8_t gazeOffset = 0;
  if (sidewaysTilt >= config::IMU_GAZE_TILT_G) {
    gazeOffset = 4 * config::IMU_GAZE_X_SIGN;
  } else if (sidewaysTilt <= -config::IMU_GAZE_TILT_G) {
    gazeOffset = -4 * config::IMU_GAZE_X_SIGN;
  }

  reading.accelX = accelX;
  reading.accelY = accelY;
  reading.accelZ = accelZ;
  reading.accelMagnitude = accelMagnitude;
  reading.gyroMagnitude = gyroMagnitude;
  reading.orientationDot = orientationDot;
  reading.gazeOffset = gazeOffset;
  reading.movementStarted = movementStarted;
  reading.pickedUp = pickedUp;
  reading.putDown = putDown;
  reading.shaken = shaken;
  reading.upsideDown = upsideDown_;
  reading.returnedUpright = wasUpsideDown && !upsideDown_;
  return true;
}
