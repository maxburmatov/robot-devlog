#include "Sensors.h"

#include <Wire.h>

#include "Config.h"

bool RobotSensors::beginLight() {
  lightAvailable_ = lightSensor_.begin(
      BH1750::CONTINUOUS_HIGH_RES_MODE,
      config::LIGHT_SENSOR_ADDRESS,
      &Wire);
  return lightAvailable_;
}

bool RobotSensors::beginDistance() {
  return distanceSensor_.begin(config::DISTANCE_SENSOR_ADDRESS, false, &Wire);
}

bool RobotSensors::readLight(float& lux) {
  if (!lightAvailable_) {
    return false;
  }

  const float measurement = lightSensor_.readLightLevel();
  if (measurement < 0) {
    return false;
  }

  lux = measurement;
  return true;
}

bool RobotSensors::readDistance(uint16_t& distanceMm, uint8_t& rangeStatus) {
  VL53L0X_RangingMeasurementData_t measurement;
  distanceSensor_.rangingTest(&measurement, false);
  rangeStatus = measurement.RangeStatus;

  if (rangeStatus != 0) {
    return false;
  }

  distanceMm = measurement.RangeMilliMeter;
  return true;
}
