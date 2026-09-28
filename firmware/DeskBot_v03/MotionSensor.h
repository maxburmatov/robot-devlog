#pragma once

#include <Arduino.h>
#include <SparkFunLSM6DS3.h>

struct MotionReading {
  float accelX = 0.0F;
  float accelY = 0.0F;
  float accelZ = 0.0F;
  float accelMagnitude = 0.0F;
  float gyroMagnitude = 0.0F;
  float orientationDot = 1.0F;
  int8_t gazeOffset = 0;
  bool movementStarted = false;
  bool pickedUp = false;
  bool putDown = false;
  bool shaken = false;
};

class MotionSensor {
 public:
  MotionSensor();

  bool begin();
  bool update(unsigned long now, MotionReading& reading);

 private:
  LSM6DS3 imu_;
  float referenceX_ = 0.0F;
  float referenceY_ = 0.0F;
  float referenceZ_ = 1.0F;
  float filteredX_ = 0.0F;
  float filteredY_ = 0.0F;
  float filteredZ_ = 1.0F;
  unsigned long lastMotionAt_ = 0;
  unsigned long pickupCandidateStartedAt_ = 0;
  unsigned long lastPickupAt_ = 0;
  unsigned long lastShakeAt_ = 0;
  bool pickupCandidate_ = false;
  bool waitingForPutDown_ = false;
  bool available_ = false;

  void calibrate();
};
