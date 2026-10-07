#pragma once

#include <Arduino.h>
#include <Wire.h>

enum class GestureDirection {
  None,
  Right,
  Left,
  Up,
  Down,
};

// Polling APDS-9960 adapter. It uses the already initialized shared Wire bus
// and normalizes the sensor board's inverted physical orientation.
class GestureSensor {
 public:
  explicit GestureSensor(TwoWire& wire = Wire);

  bool begin();
  GestureDirection read();
  uint8_t deviceId() const { return deviceId_; }
  bool isAvailable() const { return available_; }

 private:
  enum class RawGesture : int8_t {
    None = -1,
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
  };

  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegister(uint8_t reg, uint8_t& value);
  size_t readBlock(uint8_t reg, uint8_t* values, size_t length);
  bool updateRegisterBits(uint8_t reg, uint8_t mask, bool enabled);
  int fifoLevel();
  RawGesture pollRawGesture();
  static GestureDirection normalize(RawGesture raw);

  TwoWire& wire_;
  bool available_ = false;
  bool gestureEnabled_ = false;
  uint8_t deviceId_ = 0;
  bool gestureIn_ = false;
  int directionX_ = 0;
  int directionY_ = 0;
  int entryDirectionX_ = 0;
  int entryDirectionY_ = 0;
};
