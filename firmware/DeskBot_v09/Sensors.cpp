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
  const Adafruit_VL53L0X::VL53L0X_Sense_config_t mode =
      config::TOF_LONG_RANGE_MODE
          ? Adafruit_VL53L0X::VL53L0X_SENSE_LONG_RANGE
          : Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT;
  return distanceSensor_.begin(config::DISTANCE_SENSOR_ADDRESS, false, &Wire,
                               mode);
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
