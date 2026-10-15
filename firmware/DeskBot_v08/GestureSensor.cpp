#include "GestureSensor.h"

#include "Config.h"

namespace {

constexpr uint8_t REG_ENABLE = 0x80;
constexpr uint8_t REG_ATIME = 0x81;
constexpr uint8_t REG_WTIME = 0x83;
constexpr uint8_t REG_PPULSE = 0x8E;
constexpr uint8_t REG_CONTROL = 0x8F;
constexpr uint8_t REG_ID = 0x92;
constexpr uint8_t REG_GPULSE = 0xA6;
constexpr uint8_t REG_GCONF4 = 0xAB;
constexpr uint8_t REG_GFLVL = 0xAE;
constexpr uint8_t REG_GSTATUS = 0xAF;
constexpr uint8_t REG_GFIFO_U = 0xFC;

constexpr uint8_t ENABLE_POWER = 0x01;
constexpr uint8_t ENABLE_WAIT = 0x08;
constexpr uint8_t ENABLE_GESTURE = 0x40;
constexpr uint8_t GCONF4_GMODE = 0x01;
constexpr uint8_t GCONF4_GIEN = 0x02;
constexpr uint8_t GSTATUS_GVALID = 0x01;

constexpr uint8_t EXPECTED_ID = 0xAB;
constexpr uint8_t VERIFIED_MODULE_ID = 0xA8;
constexpr int GESTURE_THRESHOLD = 30;
constexpr int GESTURE_SENSITIVITY = 20;

}  // namespace

GestureSensor::GestureSensor(TwoWire& wire) : wire_(wire) {}

bool GestureSensor::begin() {
  available_ = false;
  gestureEnabled_ = false;

  // Wire.begin() is deliberately not called here: all DeskBot sensors share
  // the bus initialized once by the sketch.
  if (!readRegister(REG_ID, deviceId_) ||
      (deviceId_ != EXPECTED_ID && deviceId_ != VERIFIED_MODULE_ID)) {
    return false;
  }

  if (!writeRegister(REG_ENABLE, 0x00) ||
      !writeRegister(REG_WTIME, 0xFF) ||
      !writeRegister(REG_GPULSE, 0x8F) ||
      !writeRegister(REG_PPULSE, 0x8F) ||
      !updateRegisterBits(REG_GCONF4, GCONF4_GIEN, false) ||
      !updateRegisterBits(REG_GCONF4, GCONF4_GMODE, true) ||
      !writeRegister(REG_ATIME, 252) ||
      !writeRegister(REG_CONTROL, 0x02) ||
      !writeRegister(REG_ENABLE, ENABLE_POWER | ENABLE_WAIT |
                                     ENABLE_GESTURE)) {
    return false;
  }

  // The only delay is part of startup stabilization; read() never blocks.
  delay(10);
  gestureEnabled_ = true;
  available_ = true;
  return true;
}

GestureDirection GestureSensor::read() {
  if (!available_) {
    return GestureDirection::None;
  }
  return normalize(pollRawGesture());
}

bool GestureSensor::writeRegister(uint8_t reg, uint8_t value) {
  wire_.beginTransmission(config::GESTURE_SENSOR_ADDRESS);
  wire_.write(reg);
  wire_.write(value);
  return wire_.endTransmission() == 0;
}

bool GestureSensor::readRegister(uint8_t reg, uint8_t& value) {
  wire_.beginTransmission(config::GESTURE_SENSOR_ADDRESS);
  wire_.write(reg);
  if (wire_.endTransmission() != 0) {
    return false;
  }
  if (wire_.requestFrom(config::GESTURE_SENSOR_ADDRESS,
                        static_cast<uint8_t>(1)) != 1 || !wire_.available()) {
    return false;
  }
  value = wire_.read();
  return true;
}

size_t GestureSensor::readBlock(uint8_t reg, uint8_t* values,
                                size_t length) {
  wire_.beginTransmission(config::GESTURE_SENSOR_ADDRESS);
  wire_.write(reg);
  if (wire_.endTransmission() != 0) {
    return 0;
  }

  const size_t requested = wire_.requestFrom(
      config::GESTURE_SENSOR_ADDRESS, static_cast<uint8_t>(length));
  size_t count = 0;
  while (wire_.available() && count < length) {
    values[count++] = wire_.read();
  }
  return requested == count ? count : 0;
}

bool GestureSensor::updateRegisterBits(uint8_t reg, uint8_t mask,
                                       bool enabled) {
  uint8_t value = 0;
  if (!readRegister(reg, value)) {
    return false;
  }
  value = enabled ? (value | mask) : (value & ~mask);
  return writeRegister(reg, value);
}

int GestureSensor::fifoLevel() {
  uint8_t status = 0;
  uint8_t level = 0;
  if (!readRegister(REG_GSTATUS, status) ||
      (status & GSTATUS_GVALID) == 0 ||
      !readRegister(REG_GFLVL, level)) {
    return 0;
  }
  return level;
}

GestureSensor::RawGesture GestureSensor::pollRawGesture() {
  if (!gestureEnabled_) {
    return RawGesture::None;
  }

  const int availableSamples = fifoLevel();
  if (availableSamples <= 0) {
    return RawGesture::None;
  }

  // One FIFO batch per loop iteration keeps polling bounded and non-blocking.
  uint8_t fifoData[128];
  const size_t requestedBytes =
      min(static_cast<size_t>(availableSamples) * 4U, sizeof(fifoData));
  const size_t bytesRead =
      readBlock(REG_GFIFO_U, fifoData, requestedBytes);
  RawGesture detected = RawGesture::None;

  for (size_t i = 0; i + 3 < bytesRead; i += 4) {
    const uint8_t up = fifoData[i];
    const uint8_t down = fifoData[i + 1];
    const uint8_t left = fifoData[i + 2];
    const uint8_t right = fifoData[i + 3];

    if (up < GESTURE_THRESHOLD && down < GESTURE_THRESHOLD &&
        left < GESTURE_THRESHOLD && right < GESTURE_THRESHOLD) {
      gestureIn_ = true;
      if (entryDirectionX_ != 0 || entryDirectionY_ != 0) {
        detected = accumulatedGesture();
        resetAccumulatedGesture();
        if (detected != RawGesture::None) break;
      }
      continue;
    }

    directionX_ = static_cast<int>(right) - left;
    directionY_ = static_cast<int>(up) - down;
    if (gestureIn_) {
      gestureIn_ = false;
      entryDirectionX_ = directionX_;
      entryDirectionY_ = directionY_;
    }

    // Do not wait for the hand to leave the sensor completely. As soon as a
    // clear dominant-axis displacement is present, consume the gesture and
    // discard the rest of this FIFO batch. This removes the noticeable
    // trailing delay without adding a blocking read loop.
    if (entryDirectionX_ != 0 || entryDirectionY_ != 0) {
      detected = accumulatedGesture();
      if (detected != RawGesture::None) {
        resetAccumulatedGesture();
        break;
      }
    }
  }

  return detected;
}

GestureSensor::RawGesture GestureSensor::accumulatedGesture() const {
  const int totalX = entryDirectionX_ - directionX_;
  const int totalY = entryDirectionY_ - directionY_;
  const int magnitudeX = abs(totalX);
  const int magnitudeY = abs(totalY);

  if (magnitudeX <= GESTURE_SENSITIVITY &&
      magnitudeY <= GESTURE_SENSITIVITY) {
    return RawGesture::None;
  }

  if (magnitudeX >= magnitudeY) {
    return totalX < 0 ? RawGesture::Left : RawGesture::Right;
  }
  return totalY < 0 ? RawGesture::Down : RawGesture::Up;
}

void GestureSensor::resetAccumulatedGesture() {
  gestureIn_ = false;
  directionX_ = 0;
  directionY_ = 0;
  entryDirectionX_ = 0;
  entryDirectionY_ = 0;
}

GestureDirection GestureSensor::normalize(RawGesture raw) {
  // The verified module is mounted inverted on both axes.
  switch (raw) {
    case RawGesture::Left:
      return GestureDirection::Right;
    case RawGesture::Right:
      return GestureDirection::Left;
    case RawGesture::Up:
      return GestureDirection::Down;
    case RawGesture::Down:
      return GestureDirection::Up;
    case RawGesture::None:
      return GestureDirection::None;
  }
  return GestureDirection::None;
}
