#include <Arduino.h>
#include <Wire.h>

#include "BoredomBehavior.h"
#include "Config.h"
#include "Display.h"
#include "MotionSensor.h"
#include "RobotState.h"
#include "Sensors.h"
#include "SoundSystem.h"

RobotDisplay display;
RobotSensors sensors;
MotionSensor motionSensor;
SoundSystem sound;

const BoredomConfig boredomConfig = {
    config::HAPPY_DISTANCE_MM,
    config::CURIOUS_DISTANCE_MM,
    config::DISTANCE_CONFIRM_SAMPLES,
    config::OBJECT_LOST_TIMEOUT_MS,
    config::BOREDOM_BORED_MS,
    config::CURIOUS_REACTION_MS,
    config::BOREDOM_OFFENDED_MS,
    config::BOREDOM_RECONCILE_MS,
    config::HAPPY_DURATION_MS,
};
BoredomBehavior boredomBehavior(boredomConfig);

RobotState currentState = RobotState::Curious;
RobotState reactionState = RobotState::Surprised;
MotionReading motionReading;
bool motionSensorAvailable = false;

enum class MotionSequence {
  None,
  GentleSurprised,
  GentleCurious,
  StandaloneDizzy,
};

unsigned long lastSensorUpdate = 0;
unsigned long lastLightUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastDistanceLog = 0;
unsigned long lastImuUpdate = 0;
unsigned long lastImuLog = 0;
unsigned long wakeStartedAt = 0;
unsigned long drowsyStartedAt = 0;
unsigned long reactionUntil = 0;
unsigned long awakeUntil = 0;
unsigned long putDownSettlesAt = 0;
bool pickedUpActive = false;
bool shakenWhilePickedUp = false;
MotionSequence motionSequence = MotionSequence::None;

void updateMotionSequence(unsigned long now);

bool deadlineIsActive(unsigned long now, unsigned long deadline) {
  return deadline != 0 && static_cast<long>(deadline - now) > 0;
}

bool sleepStateIsActive() {
  return currentState == RobotState::Drowsy ||
         currentState == RobotState::Sleep ||
         currentState == RobotState::Wake;
}

bool presenceUpdatesPaused(unsigned long now) {
  return pickedUpActive || motionSequence == MotionSequence::StandaloneDizzy ||
         deadlineIsActive(now, putDownSettlesAt);
}

const char* behaviorName(RobotState state) {
  switch (state) {
    case RobotState::Curious:
      return "CURIOUS";
    case RobotState::Waiting:
      return "WAITING";
    case RobotState::Bored:
      return "BORED";
    case RobotState::Offended:
      return "OFFENDED";
    case RobotState::Reconciling:
      return "RECONCILING";
    case RobotState::Happy:
      return "HAPPY";
    case RobotState::Content:
      return "CONTENT";
    default:
      return nullptr;
  }
}

void applyBehaviorUpdate(const BoredomUpdate& update) {
  switch (update.presenceEvent) {
    case PresenceEvent::Confirmed:
      Serial.println("PRESENCE: CONFIRMED");
      break;
    case PresenceEvent::Lost:
      Serial.println("PRESENCE: LOST");
      break;
    case PresenceEvent::Returned:
      Serial.println("PRESENCE: RETURNED");
      break;
    case PresenceEvent::SensorUnavailable:
      Serial.println("PRESENCE: SENSOR_UNAVAILABLE");
      break;
    case PresenceEvent::None:
      break;
  }

  if (update.behaviorChanged) {
    const char* name = behaviorName(update.behavior);
    if (name != nullptr) {
      Serial.printf("BEHAVIOR: %s\n", name);
    }
  }
  currentState = update.behavior;
}

void resetPresenceBehavior(unsigned long now, bool logSleepReset) {
  boredomBehavior.reset(now);
  if (logSleepReset) {
    Serial.println("BEHAVIOR: RESET (SLEEP)");
  }
}

void beginWakeSequence(unsigned long now, const char* transition) {
  boredomBehavior.reset(now);
  currentState = RobotState::Wake;
  wakeStartedAt = now;
  awakeUntil = now + config::AWAKE_AFTER_WAKE_MS;

  reactionUntil = 0;
  putDownSettlesAt = 0;
  pickedUpActive = false;
  shakenWhilePickedUp = false;
  motionSequence = MotionSequence::None;

  Serial.println(transition);
}

void startReaction(RobotState state, unsigned long now,
                   unsigned long duration) {
  reactionState = state;
  reactionUntil = now + duration;
}

RobotState stateForDisplay(unsigned long now) {
  if (sleepStateIsActive()) {
    return currentState;
  }
  if (reactionState == RobotState::Dizzy &&
      deadlineIsActive(now, reactionUntil)) {
    return RobotState::Dizzy;
  }
  if (motionSensorAvailable && motionReading.upsideDown) {
    return RobotState::UpsideDown;
  }
  if (pickedUpActive) {
    return RobotState::PickedUp;
  }
  if (deadlineIsActive(now, reactionUntil)) {
    return reactionState;
  }
  return currentState;
}

void updateLightSensor(unsigned long now) {
  if (presenceUpdatesPaused(now)) {
    return;
  }

  float lightLux = 0.0F;
  if (!sensors.readLight(lightLux)) {
    Serial.println("BH1750 read error");
    return;
  }

  Serial.printf("Light: %.1f lx\n", lightLux);

  if (currentState == RobotState::Drowsy) {
    if (lightLux > config::WAKE_LUX) {
      beginWakeSequence(now, "STATE: DROWSY -> WAKE (light)");
    } else if (now - drowsyStartedAt >= config::DROWSY_DURATION_MS) {
      resetPresenceBehavior(now, true);
      currentState = RobotState::Sleep;
      Serial.println("STATE: DROWSY -> SLEEP");
    }
    return;
  }

  if (currentState != RobotState::Sleep &&
      currentState != RobotState::Wake &&
      !deadlineIsActive(now, awakeUntil) &&
      lightLux < config::SLEEP_LUX) {
    resetPresenceBehavior(now, false);
    currentState = RobotState::Drowsy;
    drowsyStartedAt = now;
    reactionUntil = 0;
    Serial.println("STATE: -> DROWSY");
    return;
  }

  if (currentState == RobotState::Sleep && lightLux > config::WAKE_LUX) {
    beginWakeSequence(now, "STATE: SLEEP -> WAKE (light)");
  }
}

void updateDistanceSensor(unsigned long now) {
  if (sleepStateIsActive() || presenceUpdatesPaused(now)) {
    return;
  }

  uint16_t distanceMm = 0;
  uint8_t rangeStatus = 255;
  const bool valid = sensors.readDistance(distanceMm, rangeStatus);

  if (now - lastDistanceLog >= config::DISTANCE_LOG_INTERVAL_MS) {
    lastDistanceLog = now;
    if (valid) {
      Serial.printf("ToF: %u mm status=%u\n", distanceMm, rangeStatus);
    } else {
      Serial.printf("ToF: invalid status=%u\n", rangeStatus);
    }
  }

  applyBehaviorUpdate(
      boredomBehavior.updateMeasurement(valid, distanceMm, now));
}

void updateWakeState(unsigned long now) {
  if (currentState == RobotState::Wake &&
      now - wakeStartedAt >= config::WAKE_DURATION_MS) {
    resetPresenceBehavior(now, false);
    currentState = RobotState::Curious;
    Serial.println("STATE: WAKE -> CURIOUS");
  }
}

void updateMotionSensor(unsigned long now) {
  if (!motionSensorAvailable || !motionSensor.update(now, motionReading)) {
    return;
  }

  if (!sleepStateIsActive()) {
    if (motionReading.putDown && pickedUpActive) {
      pickedUpActive = false;
      reactionUntil = 0;
      boredomBehavior.resume(now);
      putDownSettlesAt = now + config::PUT_DOWN_SETTLE_MS;
      awakeUntil = now + config::AWAKE_AFTER_WAKE_MS;

      if (shakenWhilePickedUp) {
        motionSequence = MotionSequence::None;
        applyBehaviorUpdate(
            boredomBehavior.handlePhysicalInteraction(true, now));
        Serial.println("EVENT: PUT_DOWN -> OFFENDED (after shake)");
      } else {
        applyBehaviorUpdate(
            boredomBehavior.handlePhysicalInteraction(false, now));
        startReaction(RobotState::Surprised, now,
                      config::SURPRISED_DURATION_MS);
        motionSequence = MotionSequence::GentleSurprised;
        Serial.println("EVENT: PUT_DOWN -> SURPRISED");
      }
      shakenWhilePickedUp = false;
    } else if (motionReading.pickedUp) {
      pickedUpActive = true;
      shakenWhilePickedUp = false;
      motionSequence = MotionSequence::None;
      reactionUntil = 0;
      putDownSettlesAt = 0;
      boredomBehavior.pause(now);
      Serial.println("REACTION: PICKED_UP (until put down)");
    } else if (motionReading.shaken) {
      if (pickedUpActive) {
        shakenWhilePickedUp = true;
        motionSequence = MotionSequence::None;
      } else {
        motionSequence = MotionSequence::StandaloneDizzy;
        boredomBehavior.pause(now);
      }
      startReaction(RobotState::Dizzy, now, config::DIZZY_DURATION_MS);
      Serial.println("REACTION: DIZZY (shake)");
    } else if (!pickedUpActive &&
               (motionReading.movementStarted ||
                motionReading.returnedUpright)) {
      startReaction(RobotState::Surprised, now,
                    config::SURPRISED_DURATION_MS);
      Serial.println("REACTION: SURPRISED (movement)");
    }
  }

  if (now - lastImuLog >= config::IMU_LOG_INTERVAL_MS) {
    lastImuLog = now;
    Serial.printf(
        "IMU a=(%.2f, %.2f, %.2f) |a|=%.2f gyro=%.1f dot=%.2f%s\n",
        motionReading.accelX,
        motionReading.accelY,
        motionReading.accelZ,
        motionReading.accelMagnitude,
        motionReading.gyroMagnitude,
        motionReading.orientationDot,
        motionReading.upsideDown ? " UPSIDE_DOWN" : "");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nDESKBOT EMOTION SYSTEM v0.5");

  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_FREQUENCY_HZ);

  display.begin();
  display.showStartup();
  delay(1000);

  if (sensors.beginLight()) {
    Serial.println("BH1750 initialized at 0x23");
  } else {
    Serial.println("BH1750 NOT FOUND");
  }

  if (!sensors.beginDistance()) {
    Serial.println("VL53L0X NOT FOUND");
    display.showDistanceSensorError();
    while (true) {
      delay(100);
    }
  }

  Serial.println("Keep DeskBot still: calibrating LSM6DS3...");
  motionSensorAvailable = motionSensor.begin();
  if (motionSensorAvailable) {
    Serial.println("LSM6DS3 initialized at 0x6B");
  } else {
    Serial.println("LSM6DS3 NOT FOUND at 0x6B");
  }

  Serial.println("OLED: 0x3C");
  Serial.println("BH1750: 0x23");
  Serial.println("VL53L0X: 0x29");
  Serial.println("LSM6DS3: 0x6B");
  Serial.printf("PRESENCE THRESHOLDS: near<=%u mm far>=%u mm\n",
                config::HAPPY_DISTANCE_MM,
                config::CURIOUS_DISTANCE_MM);
  Serial.printf("VL53L0X MODE: %s\n",
                config::TOF_LONG_RANGE_MODE ? "LONG_RANGE" : "DEFAULT");

  sound.begin();

  const unsigned long now = millis();
  beginWakeSequence(now, "STATE: STARTUP -> WAKE");
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
  updateMotionSequence(now);

  if (now - lastSensorUpdate >= config::SENSOR_INTERVAL_MS) {
    lastSensorUpdate = now;
    updateDistanceSensor(now);
  }

  if (!sleepStateIsActive() && !presenceUpdatesPaused(now)) {
    applyBehaviorUpdate(boredomBehavior.update(now));
  }

  if (now - lastDisplayUpdate >= config::DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    const RobotState visibleState = stateForDisplay(now);
    unsigned long stateStartedAt = boredomBehavior.stateEnteredAt();
    if (visibleState == RobotState::Drowsy) {
      stateStartedAt = drowsyStartedAt;
    } else if (visibleState == RobotState::Wake) {
      stateStartedAt = wakeStartedAt;
    }
    const bool phraseChanged =
        display.update(visibleState, now, stateStartedAt,
                       boredomBehavior.stateRevision(),
                       motionReading.gazeOffset);
    if (phraseChanged) {
      sound.play(visibleState);
    }
  }
}

void updateMotionSequence(unsigned long now) {
  if (deadlineIsActive(now, reactionUntil) || reactionUntil == 0) {
    return;
  }

  reactionUntil = 0;
  switch (motionSequence) {
    case MotionSequence::GentleSurprised:
      startReaction(RobotState::Curious, now,
                    config::CURIOUS_REACTION_MS);
      motionSequence = MotionSequence::GentleCurious;
      Serial.println("REACTION: SURPRISED -> CURIOUS");
      break;
    case MotionSequence::GentleCurious:
      motionSequence = MotionSequence::None;
      Serial.println("REACTION: CURIOUS -> BEHAVIOR");
      break;
    case MotionSequence::StandaloneDizzy:
      motionSequence = MotionSequence::None;
      boredomBehavior.resume(now);
      putDownSettlesAt = now + config::PUT_DOWN_SETTLE_MS;
      awakeUntil = now + config::AWAKE_AFTER_WAKE_MS;
      applyBehaviorUpdate(
          boredomBehavior.handlePhysicalInteraction(true, now));
      Serial.println("REACTION: DIZZY -> OFFENDED");
      break;
    case MotionSequence::None:
      // A held robot returns from Dizzy to persistent PickedUp.
      break;
  }
}
