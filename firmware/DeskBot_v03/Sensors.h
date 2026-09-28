#pragma once

#include <Adafruit_VL53L0X.h>
#include <BH1750.h>

class RobotSensors {
 public:
  bool beginLight();
  bool beginDistance();
  bool readLight(float& lux);
  bool readDistance(uint16_t& distanceMm, uint8_t& rangeStatus);

 private:
  Adafruit_VL53L0X distanceSensor_;
  BH1750 lightSensor_;
  bool lightAvailable_ = false;
};
