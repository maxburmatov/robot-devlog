#include <Arduino.h>
#include <Wire.h>

#include "BoredomBehavior.h"
#include "Config.h"
#include "Display.h"
#include "GestureSensor.h"
#include "MicrophoneSensor.h"
#include "MusicAnalyzer.h"
#include "MotionSensor.h"
#include "RobotState.h"
#include "Sensors.h"
#include "SoundSystem.h"
#include "TimeService.h"
#include "WeatherService.h"

RobotDisplay display;
RobotSensors sensors;
MotionSensor motionSensor;
SoundSystem sound;
GestureSensor gestureSensor;
MicrophoneSensor microphone;
MusicAnalyzer musicAnalyzer;
TimeService timeService;
WeatherService weatherService;

enum class DisplayMode {
  Face,
  Clock,
  Weather,
};

DisplayMode displayMode = DisplayMode::Face;
bool clockNeedsRedraw = true;
bool weatherNeedsRedraw = true;
uint32_t displayedWeatherRevision = UINT32_MAX;
bool clockWasValid = false;
int clockHour = -1;
int clockMinute = -1;
int clockSecond = -1;
int clockWeekday = -1;
int clockDay = -1;
int clockMonth = -1;

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

enum class ReactionSource {
  None,
  Motion,
  MicrophoneListening,
  MicrophoneClap,
};

ReactionSource reactionSource = ReactionSource::None;
bool suppressMicrophoneReactionAudio = false;
bool microphoneReactionWasDisplayed = false;
bool speakerWasPlaying = false;
unsigned long microphoneAudioGuardUntil = 0;
MusicSnapshot musicSnapshot;
bool musicWasActive = false;
unsigned long musicVisualUntil = 0;
uint32_t reportedDroppedMusicFrames = 0;
unsigned long lastMusicDropLog = 0;

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
unsigned long lastGestureUpdate = 0;
unsigned long wakeStartedAt = 0;
unsigned long drowsyStartedAt = 0;
unsigned long reactionUntil = 0;
unsigned long awakeUntil = 0;
unsigned long putDownSettlesAt = 0;
bool pickedUpActive = false;
bool shakenWhilePickedUp = false;
MotionSequence motionSequence = MotionSequence::None;

void updateMotionSequence(unsigned long now);

void handleGesture(GestureDirection gesture) {
  switch (gesture) {
    case GestureDirection::Right:
      if (displayMode == DisplayMode::Face) {
        display.startScreenTransition(false, millis());
        displayMode = DisplayMode::Clock;
        clockNeedsRedraw = true;
        Serial.println("GESTURE: RIGHT -> CLOCK");
      } else if (displayMode == DisplayMode::Clock) {
        display.startScreenTransition(false, millis());
        displayMode = DisplayMode::Weather;
        weatherNeedsRedraw = true;
        Serial.println("GESTURE: RIGHT -> WEATHER");
      }
      break;
    case GestureDirection::Left:
      if (displayMode == DisplayMode::Weather) {
        display.startScreenTransition(true, millis());
        displayMode = DisplayMode::Clock;
        clockNeedsRedraw = true;
        Serial.println("GESTURE: LEFT -> CLOCK");
      } else if (displayMode == DisplayMode::Clock) {
        display.startScreenTransition(true, millis());
        displayMode = DisplayMode::Face;
        Serial.println("GESTURE: LEFT -> FACE");
      }
      break;
    case GestureDirection::Up:
      sound.setEnabled(true);
      if (displayMode == DisplayMode::Face) {
        display.showSoundStatus(sound.isEnabled(), millis());
      }
      Serial.println("GESTURE: UP -> SOUND ON");
      break;
    case GestureDirection::Down:
      sound.setEnabled(false);
      if (displayMode == DisplayMode::Face) {
        display.showSoundStatus(sound.isEnabled(), millis());
      }
      Serial.println("GESTURE: DOWN -> SOUND OFF");
      break;
    case GestureDirection::None:
      break;
  }
}

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
    case RobotState::Drowsy:
    case RobotState::Sleep:
    case RobotState::Wake:
    case RobotState::Surprised:
    case RobotState::Startled:
    case RobotState::PickedUp:
    case RobotState::Dizzy:
    case RobotState::UpsideDown:
    case RobotState::Listening:
    case RobotState::Dancing:
    case RobotState::Count:
      return nullptr;
  }
  return nullptr;
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
  reactionSource = ReactionSource::None;
  suppressMicrophoneReactionAudio = false;
  microphoneReactionWasDisplayed = false;
  putDownSettlesAt = 0;
  pickedUpActive = false;
  shakenWhilePickedUp = false;
  motionSequence = MotionSequence::None;

  Serial.println(transition);
}

void startReaction(RobotState state, unsigned long now,
                   unsigned long duration, ReactionSource source) {
  reactionState = state;
  reactionUntil = now + duration;
  reactionSource = source;
  suppressMicrophoneReactionAudio =
      source == ReactionSource::MicrophoneListening ||
      source == ReactionSource::MicrophoneClap;
  microphoneReactionWasDisplayed = false;
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
  if (deadlineIsActive(now, reactionUntil) &&
      reactionState != RobotState::Listening) {
    return reactionState;
  }
  if (musicSnapshot.active || deadlineIsActive(now, musicVisualUntil)) {
    return RobotState::Dancing;
  }
  if (deadlineIsActive(now, reactionUntil)) {
    return reactionState;
  }
  return currentState;
}

bool isBehaviorState(RobotState state) {
  switch (state) {
    case RobotState::Curious:
    case RobotState::Waiting:
    case RobotState::Bored:
    case RobotState::Offended:
    case RobotState::Reconciling:
    case RobotState::Happy:
    case RobotState::Content:
      return true;
    case RobotState::Drowsy:
    case RobotState::Sleep:
    case RobotState::Wake:
    case RobotState::Surprised:
    case RobotState::Startled:
    case RobotState::PickedUp:
    case RobotState::Dizzy:
    case RobotState::UpsideDown:
    case RobotState::Listening:
    case RobotState::Dancing:
    case RobotState::Count:
      return false;
  }
  return false;
}

void logIgnoredMicrophoneReaction(const char* reason) {
  if (config::MIC_DEBUG) {
    Serial.printf("MIC REACTION IGNORED: %s\n", reason);
  }
}

void handleMicrophoneEvent(MicrophoneEvent event, unsigned long now) {
  if (event == MicrophoneEvent::None) {
    return;
  }
  if (event == MicrophoneEvent::Clap &&
      (musicSnapshot.candidate || musicSnapshot.active) &&
      now - musicSnapshot.lastBeatAt <= 250 &&
      musicSnapshot.beatStrength < 0.35F) {
    logIgnoredMicrophoneReaction("MUSIC BEAT, NOT CLAP");
    return;
  }
  if (sound.isPlaying()) {
    microphone.setSuppressed(true);
    logIgnoredMicrophoneReaction("AUDIO PLAYBACK");
    return;
  }
  if (!isBehaviorState(currentState)) {
    logIgnoredMicrophoneReaction("SLEEP/WAKE");
    return;
  }
  if (pickedUpActive || motionSequence != MotionSequence::None ||
      (motionSensorAvailable && motionReading.upsideDown)) {
    logIgnoredMicrophoneReaction("ACTIVE MOTION");
    return;
  }

  const bool reactionActive = deadlineIsActive(now, reactionUntil);
  if (reactionActive) {
    if (reactionSource == ReactionSource::MicrophoneListening &&
        event == MicrophoneEvent::Clap) {
      Serial.println("MIC EVENT: CLAP");
      startReaction(RobotState::Startled, now,
                    config::STARTLED_DURATION_MS,
                    ReactionSource::MicrophoneClap);
      if (sound.isEnabled()) {
        sound.playClapReaction();
      }
      Serial.println("MIC REACTION: CLAP -> STARTLED");
      return;
    }
    logIgnoredMicrophoneReaction("ACTIVE REACTION");
    return;
  }

  switch (event) {
    case MicrophoneEvent::Sound:
      if (musicSnapshot.active) {
        logIgnoredMicrophoneReaction("MUSIC ACTIVE");
        break;
      }
      Serial.println("MIC EVENT: SOUND");
      startReaction(RobotState::Listening, now,
                    config::LISTENING_DURATION_MS,
                    ReactionSource::MicrophoneListening);
      Serial.println("MIC REACTION: SOUND -> LISTENING");
      break;
    case MicrophoneEvent::Clap:
      Serial.println("MIC EVENT: CLAP");
      startReaction(RobotState::Startled, now,
                    config::STARTLED_DURATION_MS,
                    ReactionSource::MicrophoneClap);
      if (sound.isEnabled()) {
        sound.playClapReaction();
      }
      Serial.println("MIC REACTION: CLAP -> STARTLED");
      break;
    case MicrophoneEvent::None:
      break;
  }
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
    reactionSource = ReactionSource::None;
    suppressMicrophoneReactionAudio = false;
    microphoneReactionWasDisplayed = false;
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
                      config::SURPRISED_DURATION_MS,
                      ReactionSource::Motion);
        motionSequence = MotionSequence::GentleSurprised;
        Serial.println("EVENT: PUT_DOWN -> SURPRISED");
      }
      shakenWhilePickedUp = false;
    } else if (motionReading.pickedUp) {
      pickedUpActive = true;
      shakenWhilePickedUp = false;
      motionSequence = MotionSequence::None;
      reactionUntil = 0;
      reactionSource = ReactionSource::None;
      suppressMicrophoneReactionAudio = false;
      microphoneReactionWasDisplayed = false;
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
      startReaction(RobotState::Dizzy, now, config::DIZZY_DURATION_MS,
                    ReactionSource::Motion);
      Serial.println("REACTION: DIZZY (shake)");
    } else if (!pickedUpActive &&
               (motionReading.movementStarted ||
                motionReading.returnedUpright)) {
      startReaction(RobotState::Surprised, now,
                    config::SURPRISED_DURATION_MS,
                    ReactionSource::Motion);
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
  Serial.println("\nDESKBOT EMOTION SYSTEM v0.7");

  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_FREQUENCY_HZ);

  display.begin();
  display.showStartup();
  delay(1000);

  if (gestureSensor.begin()) {
    Serial.printf("APDS-9960 initialized at 0x%02X, ID=0x%02X\n",
                  config::GESTURE_SENSOR_ADDRESS, gestureSensor.deviceId());
  } else {
    Serial.println("APDS-9960 NOT FOUND");
  }

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
  Serial.println("APDS-9960: 0x39");
  Serial.printf("PRESENCE THRESHOLDS: near<=%u mm far>=%u mm\n",
                config::HAPPY_DISTANCE_MM,
                config::CURIOUS_DISTANCE_MM);
  Serial.printf("VL53L0X MODE: %s\n",
                config::TOF_LONG_RANGE_MODE ? "LONG_RANGE" : "DEFAULT");

  sound.begin();
  Serial.println("MUSIC: initializing analyzer");
  const bool musicReady = musicAnalyzer.begin();
  Serial.printf("MUSIC: analyzer %s\n", musicReady ? "ready" : "failed");
  Serial.println("MIC: initializing ADC continuous");
  if (musicReady && microphone.begin(musicAnalyzer)) {
    Serial.printf("MIC: MAX9814 GPIO%u ADC continuous %lu Hz, FFT %u\n",
                  config::MIC_PIN,
                  static_cast<unsigned long>(config::MIC_AUDIO_SAMPLE_RATE),
                  static_cast<unsigned>(config::MUSIC_FFT_SIZE));
  } else {
    Serial.println("MIC: sampler initialization failed");
  }

  const unsigned long now = millis();
  timeService.begin(now);
  weatherService.begin(now);
  beginWakeSequence(now, "STATE: STARTUP -> WAKE");
}

void loop() {
  const unsigned long now = millis();

  // Keep sampling continuously. Suppression only clears classifier state and
  // prevents the MAX98357A speaker from creating microphone events.
  bool speakerPlaying = sound.isPlaying();
  if (speakerWasPlaying && !speakerPlaying) {
    microphoneAudioGuardUntil = now + config::MIC_SELF_AUDIO_GUARD_MS;
  }
  speakerWasPlaying = speakerPlaying;
  microphone.setSuppressed(
      speakerPlaying || deadlineIsActive(now, microphoneAudioGuardUntil));
  MicrophoneEvent microphoneEvent = microphone.update(now);
  musicSnapshot = musicAnalyzer.snapshot();
  if (config::MUSIC_DEBUG &&
      musicSnapshot.droppedFrames != reportedDroppedMusicFrames &&
      now - lastMusicDropLog >= 1000) {
    reportedDroppedMusicFrames = musicSnapshot.droppedFrames;
    lastMusicDropLog = now;
    Serial.printf("MUSIC: DROPPED_FRAMES=%lu\n",
                  static_cast<unsigned long>(reportedDroppedMusicFrames));
  }
  speakerPlaying = sound.isPlaying();
  if (speakerPlaying) {
    speakerWasPlaying = true;
    microphone.setSuppressed(true);
    microphoneEvent = MicrophoneEvent::None;
  }

  if (musicSnapshot.active != musicWasActive) {
    musicWasActive = musicSnapshot.active;
    if (musicWasActive) {
      musicVisualUntil = 0;
      Serial.printf("MUSIC: START bpm=%u confidence=%.2f\n",
                    musicSnapshot.bpm, musicSnapshot.confidence);
    } else {
      musicVisualUntil = now + config::MUSIC_EYE_SWAY_FADE_MS;
      Serial.println("MUSIC: STOP");
    }
  }

  timeService.update(now);
  weatherService.update(now);

  if (now - lastGestureUpdate >= config::GESTURE_INTERVAL_MS) {
    lastGestureUpdate = now;
    handleGesture(gestureSensor.read());
  }

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

  handleMicrophoneEvent(microphoneEvent, now);

  if (now - lastDisplayUpdate >= config::DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    if (display.updateScreenTransition(now)) {
      return;
    }
    if (displayMode == DisplayMode::Clock) {
      // State, boredom, motion and all sensor polling continue in the
      // background. The face renderer is intentionally not updated, so
      // invisible phrase changes cannot enqueue audio.
      tm localTime = {};
      const bool timeValid = timeService.getLocalTime(localTime);
      const int hour = timeValid ? localTime.tm_hour : -1;
      const int minute = timeValid ? localTime.tm_min : -1;
      const int second = timeValid ? localTime.tm_sec : -1;
      const int weekday = timeValid ? localTime.tm_wday : -1;
      const int day = timeValid ? localTime.tm_mday : -1;
      const int month = timeValid ? localTime.tm_mon + 1 : -1;
      const bool contentChanged =
          clockNeedsRedraw || timeValid != clockWasValid ||
          (timeValid && (hour != clockHour || minute != clockMinute ||
                         second != clockSecond || weekday != clockWeekday ||
                         day != clockDay || month != clockMonth));
      if (contentChanged) {
        display.showClock(timeValid, static_cast<uint8_t>(hour),
                          static_cast<uint8_t>(minute),
                          static_cast<uint8_t>(second),
                          static_cast<uint8_t>(weekday),
                          static_cast<uint8_t>(day),
                          static_cast<uint8_t>(month));
        clockNeedsRedraw = false;
        clockWasValid = timeValid;
        clockHour = hour;
        clockMinute = minute;
        clockSecond = second;
        clockWeekday = weekday;
        clockDay = day;
        clockMonth = month;
      }
      return;
    }
    if (displayMode == DisplayMode::Weather) {
      const uint32_t revision = weatherService.revision();
      if (weatherNeedsRedraw || revision != displayedWeatherRevision) {
        WeatherData weather;
        weatherService.getWeather(weather);
        display.showWeather(weather, weatherService.status());
        weatherNeedsRedraw = false;
        displayedWeatherRevision = revision;
      }
      return;
    }
    const RobotState visibleState = stateForDisplay(now);
    if (suppressMicrophoneReactionAudio &&
        deadlineIsActive(now, reactionUntil) &&
        visibleState == reactionState) {
      microphoneReactionWasDisplayed = true;
    }
    unsigned long stateStartedAt = boredomBehavior.stateEnteredAt();
    if (visibleState == RobotState::Drowsy) {
      stateStartedAt = drowsyStartedAt;
    } else if (visibleState == RobotState::Wake) {
      stateStartedAt = wakeStartedAt;
    }
    const bool phraseChanged =
        display.update(visibleState, now, stateStartedAt,
                       boredomBehavior.stateRevision(),
                       motionReading.gazeOffset, musicSnapshot.bands,
                       musicSnapshot.beatSequence,
                       musicSnapshot.beatStrength, musicSnapshot.active);
    if (phraseChanged) {
      if (visibleState == RobotState::Dancing || musicSnapshot.candidate) {
        // Dancing is visual only. During rhythm confirmation, suppress a
        // background phrase sound so self-audio cannot erase the beat history.
      } else if (suppressMicrophoneReactionAudio) {
        if (!deadlineIsActive(now, reactionUntil) ||
            visibleState != reactionState) {
          suppressMicrophoneReactionAudio = false;
          microphoneReactionWasDisplayed = false;
        }
      } else {
        sound.play(visibleState);
      }
    }
  }
}

void updateMotionSequence(unsigned long now) {
  if (deadlineIsActive(now, reactionUntil) || reactionUntil == 0) {
    return;
  }

  const bool microphoneReactionExpired =
      reactionSource == ReactionSource::MicrophoneListening ||
      reactionSource == ReactionSource::MicrophoneClap;
  reactionUntil = 0;
  reactionSource = ReactionSource::None;
  if (microphoneReactionExpired && !microphoneReactionWasDisplayed) {
    suppressMicrophoneReactionAudio = false;
  }
  switch (motionSequence) {
    case MotionSequence::GentleSurprised:
      startReaction(RobotState::Curious, now,
                    config::CURIOUS_REACTION_MS,
                    ReactionSource::Motion);
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
