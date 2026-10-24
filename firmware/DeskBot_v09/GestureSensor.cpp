#include "GestureSensor.h"

#include "Config.h"

namespace {

constexpr uint8_t REG_ENABLE = 0x80;
constexpr uint8_t REG_ATIME = 0x81;
constexpr uint8_t REG_WTIME = 0x83;
constexpr uint8_t REG_PPULSE = 0x8E;
constexpr uint8_t REG_CONTROL = 0x8F;
constexpr uint8_t REG_ID = 0x92;
constexpr uint8_t REG_GPENTH = 0xA0;
constexpr uint8_t REG_GEXTH = 0xA1;
constexpr uint8_t REG_GCONF1 = 0xA2;
constexpr uint8_t REG_GCONF2 = 0xA3;
constexpr uint8_t REG_GPULSE = 0xA6;
constexpr uint8_t REG_GCONF3 = 0xAA;
constexpr uint8_t REG_GCONF4 = 0xAB;
constexpr uint8_t REG_GFLVL = 0xAE;
constexpr uint8_t REG_GSTATUS = 0xAF;
constexpr uint8_t REG_GFIFO_U = 0xFC;

constexpr uint8_t ENABLE_POWER = 0x01;
constexpr uint8_t ENABLE_PROXIMITY = 0x04;
constexpr uint8_t ENABLE_WAIT = 0x08;
constexpr uint8_t ENABLE_GESTURE = 0x40;
constexpr uint8_t GCONF4_GMODE = 0x01;
constexpr uint8_t GCONF4_GIEN = 0x02;
constexpr uint8_t GSTATUS_GVALID = 0x01;

constexpr uint8_t EXPECTED_ID = 0xAB;
constexpr uint8_t VERIFIED_MODULE_ID = 0xA8;
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

  // Use the gesture settings recommended by the APDS-9960 reference driver.
  // Proximity enable is mandatory for the gesture engine; v0.8 omitted it,
  // which made FIFO acquisition depend on residual power-on register state.
  if (!writeRegister(REG_ENABLE, 0x00) ||
      !writeRegister(REG_WTIME, 0xFF) ||
      !writeRegister(REG_PPULSE, 0x89) ||
      !writeRegister(REG_GPENTH, 40) ||
      !writeRegister(REG_GEXTH, 30) ||
      !writeRegister(REG_GCONF1, 0x40) ||
      !writeRegister(REG_GCONF2, 0x41) ||
      !writeRegister(REG_GPULSE, 0xC9) ||
      !writeRegister(REG_GCONF3, 0x00) ||
      !updateRegisterBits(REG_GCONF4, GCONF4_GIEN, false) ||
      !updateRegisterBits(REG_GCONF4, GCONF4_GMODE, true) ||
      !writeRegister(REG_ATIME, 219) ||
      !writeRegister(REG_CONTROL, 0x02) ||
      !writeRegister(REG_ENABLE, ENABLE_POWER | ENABLE_PROXIMITY |
                                     ENABLE_WAIT | ENABLE_GESTURE)) {
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

  const unsigned long now = millis();
  if (static_cast<int32_t>(now - cooldownUntil_) < 0) {
    return RawGesture::None;
  }

  const int availableSamples = fifoLevel();
  if (availableSamples <= 0) {
    if (sampleCount_ >= config::GESTURE_MIN_SAMPLES &&
        now - lastSampleAt_ >= config::GESTURE_END_TIMEOUT_MS) {
      const RawGesture detected = accumulatedGesture();
      resetAccumulatedGesture();
      if (detected != RawGesture::None) {
        cooldownUntil_ = now + config::GESTURE_COOLDOWN_MS;
      }
      return detected;
    }
    return RawGesture::None;
  }

  // One FIFO batch per loop iteration keeps polling bounded and non-blocking.
  uint8_t fifoData[128];
  const size_t requestedBytes =
      min(static_cast<size_t>(availableSamples) * 4U, sizeof(fifoData));
  const size_t bytesRead =
      readBlock(REG_GFIFO_U, fifoData, requestedBytes);
  for (size_t i = 0; i + 3 < bytesRead; i += 4) {
    const uint8_t up = fifoData[i];
    const uint8_t down = fifoData[i + 1];
    const uint8_t left = fifoData[i + 2];
    const uint8_t right = fifoData[i + 3];

    addSample(up, down, left, right, now);
  }

  // A strong, dominant displacement is safe to publish before GVALID falls;
  // otherwise the timeout path above completes the gesture without blocking.
  const RawGesture detected = accumulatedGesture();
  if (detected != RawGesture::None) {
    resetAccumulatedGesture();
    cooldownUntil_ = now + config::GESTURE_COOLDOWN_MS;
  }
  return detected;
}

GestureSensor::RawGesture GestureSensor::accumulatedGesture() const {
  if (sampleCount_ < config::GESTURE_MIN_SAMPLES) {
    return RawGesture::None;
  }

  const int totalX = firstXRatio_ - lastXRatio_;
  const int totalY = firstYRatio_ - lastYRatio_;
  const int magnitudeX = abs(totalX);
  const int magnitudeY = abs(totalY);

  if (magnitudeX < config::GESTURE_DIRECTION_THRESHOLD &&
      magnitudeY < config::GESTURE_DIRECTION_THRESHOLD) {
    return RawGesture::None;
  }

  if (magnitudeX >= config::GESTURE_DIRECTION_THRESHOLD &&
      magnitudeX >= magnitudeY * config::GESTURE_DOMINANT_AXIS_RATIO) {
    return totalX < 0 ? RawGesture::Left : RawGesture::Right;
  }
  if (magnitudeY >= config::GESTURE_DIRECTION_THRESHOLD &&
      magnitudeY >= magnitudeX * config::GESTURE_DOMINANT_AXIS_RATIO) {
    return totalY < 0 ? RawGesture::Down : RawGesture::Up;
  }
  return RawGesture::None;
}

void GestureSensor::addSample(uint8_t up, uint8_t down, uint8_t left,
                              uint8_t right, unsigned long now) {
  if (up < config::GESTURE_MIN_SAMPLE_VALUE ||
      down < config::GESTURE_MIN_SAMPLE_VALUE ||
      left < config::GESTURE_MIN_SAMPLE_VALUE ||
      right < config::GESTURE_MIN_SAMPLE_VALUE) {
    return;
  }

  const int upDownTotal = static_cast<int>(up) + down;
  const int leftRightTotal = static_cast<int>(left) + right;
  if (upDownTotal == 0 || leftRightTotal == 0) return;

  const int16_t xRatio = static_cast<int16_t>(
      100L * (static_cast<int>(left) - right) / leftRightTotal);
  const int16_t yRatio = static_cast<int16_t>(
      100L * (static_cast<int>(up) - down) / upDownTotal);
  if (sampleCount_ == 0) {
    firstXRatio_ = xRatio;
    firstYRatio_ = yRatio;
  }
  lastXRatio_ = xRatio;
  lastYRatio_ = yRatio;
  if (sampleCount_ != UINT8_MAX) ++sampleCount_;
  lastSampleAt_ = now;
}

void GestureSensor::resetAccumulatedGesture() {
  sampleCount_ = 0;
  firstXRatio_ = 0;
  firstYRatio_ = 0;
  lastXRatio_ = 0;
  lastYRatio_ = 0;
  lastSampleAt_ = 0;
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
