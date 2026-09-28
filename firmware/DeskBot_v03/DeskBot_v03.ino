#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Display.h"
#include "MotionSensor.h"
#include "RobotState.h"
#include "Sensors.h"

RobotDisplay display;
RobotSensors sensors;
MotionSensor motionSensor;
RobotState currentState = RobotState::Curious;
RobotState reactionState = RobotState::Surprised;
MotionReading motionReading;
bool motionSensorAvailable = false;

unsigned long lastSensorUpdate = 0;
unsigned long lastLightUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastDistanceLog = 0;
unsigned long lastImuUpdate = 0;
unsigned long lastImuLog = 0;
unsigned long lastValidMeasurement = 0;
unsigned long wakeStartedAt = 0;
unsigned long drowsyStartedAt = 0;
unsigned long reactionUntil = 0;
unsigned long awakeUntil = 0;
uint8_t nearDistanceSamples = 0;
uint8_t farDistanceSamples = 0;

bool deadlineIsActive(unsigned long now, unsigned long deadline) {
  return static_cast<long>(deadline - now) > 0;
}

void startReaction(RobotState state, unsigned long now,
                   unsigned long duration) {
  reactionState = state;
  reactionUntil = now + duration;
}

RobotState stateForDisplay(unsigned long now) {
  if (deadlineIsActive(now, reactionUntil)) {
    return reactionState;
  }
  return currentState;
}

void updateLightSensor(unsigned long now) {
  float lightLux = 0.0F;
  if (!sensors.readLight(lightLux)) {
    Serial.println("Ошибка чтения BH1750");
    return;
  }

  Serial.printf("Освещённость: %.1f лк\n", lightLux);

  // Только свет может отменить засыпание до перехода в Sleep.
  if (currentState == RobotState::Drowsy) {
    if (lightLux > config::WAKE_LUX) {
      currentState = RobotState::Curious;
      Serial.println("STATE: DROWSY -> CURIOUS (свет)");
    } else if (now - drowsyStartedAt >= config::DROWSY_DURATION_MS) {
      currentState = RobotState::Sleep;
      Serial.println("STATE: DROWSY -> SLEEP");
    }
    return;
  }

  if (currentState != RobotState::Sleep &&
      currentState != RobotState::Wake &&
      !deadlineIsActive(now, awakeUntil) &&
      lightLux < config::SLEEP_LUX) {
    currentState = RobotState::Drowsy;
    drowsyStartedAt = now;
    reactionUntil = 0;
    Serial.println("STATE: -> DROWSY");
    return;
  }

  if (currentState == RobotState::Sleep && lightLux > config::WAKE_LUX) {
    currentState = RobotState::Wake;
    wakeStartedAt = now;
    awakeUntil = now + config::AWAKE_AFTER_WAKE_MS;
    Serial.println("STATE: SLEEP -> WAKE");
  }
}

void updateDistanceSensor(unsigned long now) {
  if (currentState == RobotState::Sleep ||
      currentState == RobotState::Drowsy ||
      currentState == RobotState::Wake) {
    nearDistanceSamples = 0;
    farDistanceSamples = 0;
    return;
  }

  uint16_t distanceMm = 0;
  uint8_t rangeStatus = 255;
  if (sensors.readDistance(distanceMm, rangeStatus)) {
    lastValidMeasurement = now;

    if (now - lastDistanceLog >= config::DISTANCE_LOG_INTERVAL_MS) {
      lastDistanceLog = now;
      Serial.printf("ToF: %u мм, статус=%u\n", distanceMm, rangeStatus);
    }

    if (distanceMm <= config::HAPPY_DISTANCE_MM) {
      farDistanceSamples = 0;
      if (nearDistanceSamples < config::DISTANCE_CONFIRM_SAMPLES) {
        ++nearDistanceSamples;
      }

      if (currentState == RobotState::Curious &&
          nearDistanceSamples >= config::DISTANCE_CONFIRM_SAMPLES) {
        currentState = RobotState::Happy;
        nearDistanceSamples = 0;
        Serial.println("STATE: CURIOUS -> HAPPY (расстояние подтверждено)");
      }
    } else if (distanceMm >= config::CURIOUS_DISTANCE_MM) {
      nearDistanceSamples = 0;
      if (farDistanceSamples < config::DISTANCE_CONFIRM_SAMPLES) {
        ++farDistanceSamples;
      }

      if (currentState == RobotState::Happy &&
          farDistanceSamples >= config::DISTANCE_CONFIRM_SAMPLES) {
        currentState = RobotState::Curious;
        farDistanceSamples = 0;
        Serial.println("STATE: HAPPY -> CURIOUS (расстояние подтверждено)");
      }
    } else {
      // Зона гистерезиса 300–400 мм сохраняет текущее состояние, но не
      // продолжает незавершённую серию измерений у одного из порогов.
      nearDistanceSamples = 0;
      farDistanceSamples = 0;
    }
  } else {
    nearDistanceSamples = 0;
    farDistanceSamples = 0;

    if (now - lastDistanceLog >= config::DISTANCE_LOG_INTERVAL_MS) {
      lastDistanceLog = now;
      Serial.printf("ToF: некорректный замер, статус=%u\n", rangeStatus);
    }

    if (currentState == RobotState::Happy &&
        now - lastValidMeasurement >= config::OBJECT_LOST_TIMEOUT_MS) {
      currentState = RobotState::Curious;
      Serial.println("STATE: HAPPY -> CURIOUS (объект потерян)");
    }
  }
}

void updateWakeState(unsigned long now) {
  if (currentState == RobotState::Wake &&
      now - wakeStartedAt >= config::WAKE_DURATION_MS) {
    currentState = RobotState::Curious;
    Serial.println("STATE: WAKE -> CURIOUS");
  }
}

void updateMotionSensor(unsigned long now) {
  if (!motionSensorAvailable || !motionSensor.update(now, motionReading)) {
    return;
  }

  const bool reactionsAllowed =
      currentState != RobotState::Sleep &&
      currentState != RobotState::Drowsy;

  // Во время сна и засыпания IMU не меняет состояние и эмоцию робота.
  if (reactionsAllowed) {
    if (motionReading.putDown) {
      if (reactionState == RobotState::PickedUp) {
        reactionUntil = 0;
      }
      Serial.println("EVENT: PUT_DOWN (дополнительная реакция подавлена)");
    } else if (motionReading.pickedUp) {
      startReaction(RobotState::PickedUp, now,
                    config::PICKED_UP_DURATION_MS);
      Serial.println("REACTION: PICKED_UP (поставь меня)");
    } else if (motionReading.shaken) {
      startReaction(RobotState::Dizzy, now, config::DIZZY_DURATION_MS);
      Serial.println("REACTION: DIZZY (встряхивание)");
    } else if (motionReading.movementStarted) {
      startReaction(RobotState::Surprised, now,
                    config::SURPRISED_DURATION_MS);
      Serial.println("REACTION: SURPRISED (короткое движение)");
    }
  }

  if (now - lastImuLog >= config::IMU_LOG_INTERVAL_MS) {
    lastImuLog = now;
    Serial.printf(
        "IMU a=(%.2f, %.2f, %.2f) |a|=%.2f gyro=%.1f dot=%.2f\n",
        motionReading.accelX,
        motionReading.accelY,
        motionReading.accelZ,
        motionReading.accelMagnitude,
        motionReading.gyroMagnitude,
        motionReading.orientationDot);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nСИСТЕМА ЭМОЦИЙ DESKBOT v0.3");

  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_FREQUENCY_HZ);

  display.begin();
  display.showStartup();
  delay(1000);

  if (sensors.beginLight()) {
    Serial.println("BH1750 инициализирован по адресу 0x23");
  } else {
    // Без датчика света робот продолжит работать, но не будет засыпать.
    Serial.println("BH1750 НЕ НАЙДЕН");
  }

  if (!sensors.beginDistance()) {
    Serial.println("VL53L0X НЕ НАЙДЕН");
    display.showDistanceSensorError();
    while (true) {
      delay(100);
    }
  }

  Serial.println("Не двигайте DeskBot: выполняется калибровка LSM6DS3...");
  motionSensorAvailable = motionSensor.begin();
  if (motionSensorAvailable) {
    Serial.println("LSM6DS3 инициализирован по адресу 0x6B");
  } else {
    // Исходное поведение по свету и расстоянию остаётся доступным без IMU.
    Serial.println("LSM6DS3 НЕ НАЙДЕН по адресу 0x6B");
  }

  Serial.println("OLED: 0x3C");
  Serial.println("BH1750: 0x23");
  Serial.println("VL53L0X: 0x29");
  Serial.println("LSM6DS3: 0x6B");

  currentState = RobotState::Curious;
  lastValidMeasurement = millis();
  Serial.println("STATE: CURIOUS");
  delay(500);
}

void loop() {
  const unsigned long now = millis();

  if (now - lastLightUpdate >= config::LIGHT_INTERVAL_MS) {
    lastLightUpdate = now;
    updateLightSensor(now);
  }

  updateWakeState(now);

  if (now - lastImuUpdate >= config::IMU_INTERVAL_MS) {
    lastImuUpdate = now;
    updateMotionSensor(now);
  }

  if (now - lastSensorUpdate >= config::SENSOR_INTERVAL_MS) {
    lastSensorUpdate = now;
    updateDistanceSensor(now);
  }

  if (now - lastDisplayUpdate >= config::DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    const RobotState visibleState = stateForDisplay(now);
    const unsigned long stateStartedAt =
        visibleState == RobotState::Drowsy ? drowsyStartedAt : wakeStartedAt;
    display.update(visibleState, now, stateStartedAt, motionReading.gazeOffset);
  }
}
