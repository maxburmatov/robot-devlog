#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <cstring>
#include <esp_heap_caps.h>

#include "BoredomBehavior.h"
#include "AudioCapture.h"
#include "CommandConsole.h"
#include "Config.h"
#include "Display.h"
#include "GestureSensor.h"
#include "GazeTracker.h"
#include "LightMonitor.h"
#include "LightingAutomation.h"
#include "MusicAnalyzer.h"
#include "MotionSensor.h"
#include "NeckBehaviorController.h"
#include "NeckController.h"
#include "RobotState.h"
#include "Sensors.h"
#include "SoundSystem.h"
#include "SoundDetector.h"
#include "TimeService.h"
#include "TuyaPlugClient.h"
#include "VoiceCommandDetector.h"
#include "WeatherService.h"

RobotDisplay display;
RobotSensors sensors;
MotionSensor motionSensor;
SoundSystem sound;
GestureSensor gestureSensor;
AudioCapture audioCapture;
SoundDetector soundDetector;
MusicAnalyzer musicAnalyzer;
VoiceCommandDetector voiceDetector;
TimeService timeService;
WeatherService weatherService;
LightMonitor lightMonitor;
LightingAutomation lightingAutomation;
TuyaPlugClient tuyaPlug;
NeckController neck;
NeckBehaviorController neckBehavior;
GazeTracker gazeTracker;
CommandConsole commandConsole(gazeTracker, neck, neckBehavior, lightMonitor,
                              lightingAutomation, tuyaPlug);

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
bool lightingAnnouncementWasActive = false;
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
  VoiceCommand,
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
unsigned long lastAudioHealthLog = 0;
uint64_t previousSoundBusyUs = 0;
uint64_t previousMusicBusyUs = 0;
uint64_t previousVoiceBusyUs = 0;
uint64_t previousCaptureBusyUs = 0;
uint64_t previousOutputBusyUs = 0;
uint32_t previousMusicFftFrames = 0;
uint32_t previousVoiceInferences = 0;
int16_t* datasetRecordBuffer = nullptr;
bool datasetRecording = false;
uint64_t datasetRecordEndSample = 0;
char datasetCommand[16] = {};
size_t datasetCommandLength = 0;

enum class VoiceMode {
  WaitingWakeWord,
  ListeningCommand,
};

VoiceMode voiceMode = VoiceMode::WaitingWakeWord;
bool voiceCommandClassifierEnabled = false;
unsigned long voiceCommandAcceptsAt = 0;
unsigned long voiceCommandUntil = 0;
unsigned long voiceLockoutUntil = 0;
unsigned long manualDanceUntil = 0;
unsigned long voiceScreenUntil = 0;
unsigned long voiceScreenHardUntil = 0;
unsigned long voiceTestCommandUntil = 0;
bool voiceWeatherResultSeen = false;
bool voiceWeatherCuePending = false;
bool voiceWeatherRequestObserved = false;
bool voiceTimeCuePending = false;
bool manualSleepActive = false;
bool automaticSleepActive = false;
bool autoSleepPendingLogged = false;
bool presenceDeferLogged = false;
unsigned long darkSince = 0;
unsigned long absenceSince = 0;
unsigned long lastListeningCueAt = 0;
bool hasPlayedListeningCue = false;

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
unsigned long lastLightingDiagnostic = 0;
unsigned long lastPlugStatusAttempt = 0;
bool lightReadHealthy = true;
bool tuyaTurnOnWasAutomatic = false;
LightingAutomationState lastLightingState =
    LightingAutomationState::AutoDisabled;
unsigned long wakeStartedAt = 0;
unsigned long drowsyStartedAt = 0;
unsigned long reactionUntil = 0;
unsigned long awakeUntil = 0;
unsigned long putDownSettlesAt = 0;
bool pickedUpActive = false;
bool shakenWhilePickedUp = false;
MotionSequence motionSequence = MotionSequence::None;

void updateMotionSequence(unsigned long now);
void cancelVoiceListening(const char* reason);

void handleGesture(GestureDirection gesture) {
  if (gesture != GestureDirection::None) {
    voiceScreenUntil = 0;
    voiceScreenHardUntil = 0;
  }
  switch (gesture) {
    case GestureDirection::Up:
      if (displayMode == DisplayMode::Face) {
        display.startScreenTransition(false, millis());
        displayMode = DisplayMode::Clock;
        clockNeedsRedraw = true;
        Serial.println("GESTURE: UP -> CLOCK");
      } else if (displayMode == DisplayMode::Clock) {
        display.startScreenTransition(false, millis());
        displayMode = DisplayMode::Weather;
        weatherNeedsRedraw = true;
        Serial.println("GESTURE: UP -> WEATHER");
      }
      break;
    case GestureDirection::Down:
      if (displayMode == DisplayMode::Weather) {
        display.startScreenTransition(true, millis());
        displayMode = DisplayMode::Clock;
        clockNeedsRedraw = true;
        Serial.println("GESTURE: DOWN -> CLOCK");
      } else if (displayMode == DisplayMode::Clock) {
        display.startScreenTransition(true, millis());
        displayMode = DisplayMode::Face;
        Serial.println("GESTURE: DOWN -> FACE");
      }
      break;
    case GestureDirection::Left:
      sound.setEnabled(true);
      if (displayMode == DisplayMode::Face) {
        display.showSoundStatus(sound.isEnabled(), millis());
      }
      Serial.println("GESTURE: LEFT -> SOUND ON");
      break;
    case GestureDirection::Right:
      sound.setEnabled(false);
      if (displayMode == DisplayMode::Face) {
        display.showSoundStatus(sound.isEnabled(), millis());
      }
      Serial.println("GESTURE: RIGHT -> SOUND OFF");
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

bool gazeStateAllowsControl(RobotState state) {
  return state == RobotState::Curious || state == RobotState::Waiting ||
         state == RobotState::Bored || state == RobotState::Happy ||
         state == RobotState::Content;
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
    case RobotState::Attention:
    case RobotState::Listening:
    case RobotState::Dancing:
    case RobotState::Greeting:
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

  const unsigned long now = millis();
  if (boredomBehavior.hasConfirmedPresence()) {
    absenceSince = 0;
    if (automaticSleepActive && currentState == RobotState::Drowsy) {
      automaticSleepActive = false;
      reactionUntil = 0;
      reactionSource = ReactionSource::None;
      resetPresenceBehavior(now, false);
      currentState = boredomBehavior.state();
      Serial.println("SLEEP: AUTO CANCELLED reason=PRESENCE");
    } else if (darkSince != 0 && !presenceDeferLogged) {
      presenceDeferLogged = true;
      Serial.println("SLEEP: AUTO DEFERRED reason=PRESENCE");
    }
  } else if (boredomBehavior.hasConfirmedAbsence() && absenceSince == 0) {
    absenceSince = now;
    presenceDeferLogged = false;
  } else if (update.presenceEvent == PresenceEvent::SensorUnavailable) {
    // Unknown is not absence. A fresh full delay is required after recovery.
    absenceSince = 0;
    presenceDeferLogged = false;
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
  manualSleepActive = false;
  automaticSleepActive = false;
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
      source == ReactionSource::MicrophoneClap ||
      source == ReactionSource::VoiceCommand;
  microphoneReactionWasDisplayed = false;
}

RobotState stateForDisplay(unsigned long now) {
  if (sleepStateIsActive()) {
    return currentState;
  }
  if (voiceMode == VoiceMode::ListeningCommand) {
    return RobotState::Attention;
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
      reactionState != RobotState::Listening &&
      reactionState != RobotState::Greeting) {
    return reactionState;
  }
  if (musicSnapshot.active || deadlineIsActive(now, musicVisualUntil)) {
    return RobotState::Dancing;
  }
  if (deadlineIsActive(now, manualDanceUntil)) {
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
    case RobotState::Attention:
    case RobotState::Listening:
    case RobotState::Dancing:
    case RobotState::Greeting:
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

void handleMicrophoneEvent(SoundEvent event, unsigned long now) {
  if (event == SoundEvent::None) {
    return;
  }
  if (voiceMode == VoiceMode::ListeningCommand) {
    logIgnoredMicrophoneReaction("VOICE COMMAND WINDOW");
    return;
  }
  if (event == SoundEvent::Clap &&
      (musicSnapshot.candidate || musicSnapshot.active) &&
      now - musicSnapshot.lastBeatAt <= 250 &&
      musicSnapshot.beatStrength < 0.35F) {
    logIgnoredMicrophoneReaction("MUSIC BEAT, NOT CLAP");
    return;
  }
  if (sound.isPlaying()) {
    soundDetector.setSuppressed(true);
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
        event == SoundEvent::Clap) {
      Serial.println("MIC EVENT: CLAP");
      soundDetector.cancelSoundEpisode();
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
    case SoundEvent::Sound:
      if (musicSnapshot.active) {
        logIgnoredMicrophoneReaction("MUSIC ACTIVE");
        break;
      }
      Serial.println("MIC EVENT: SOUND");
      startReaction(RobotState::Listening, now,
                    config::LISTENING_DURATION_MS,
                    ReactionSource::MicrophoneListening);
      if (!hasPlayedListeningCue ||
          now - lastListeningCueAt >= config::LISTENING_SOUND_COOLDOWN_MS) {
        if (sound.playCue(InterfaceCue::Listening)) {
          lastListeningCueAt = now;
          hasPlayedListeningCue = true;
          Serial.println("MIC: LISTENING CUE");
        }
      } else {
        Serial.println("MIC: LISTENING CUE SKIPPED reason=COOLDOWN");
      }
      Serial.println("MIC REACTION: SOUND -> LISTENING");
      break;
    case SoundEvent::Clap:
      Serial.println("MIC EVENT: CLAP");
      soundDetector.cancelSoundEpisode();
      startReaction(RobotState::Startled, now,
                    config::STARTLED_DURATION_MS,
                    ReactionSource::MicrophoneClap);
      if (sound.isEnabled()) {
        sound.playClapReaction();
      }
      Serial.println("MIC REACTION: CLAP -> STARTLED");
      break;
    case SoundEvent::None:
      break;
  }
}

bool voiceBlockedByMotion() {
  return pickedUpActive || motionSequence != MotionSequence::None ||
         (motionSensorAvailable && motionReading.upsideDown);
}

void showFaceFromVoice(unsigned long now) {
  if (displayMode != DisplayMode::Face) {
    display.startScreenTransition(true, now);
    displayMode = DisplayMode::Face;
  }
  voiceScreenUntil = 0;
  voiceScreenHardUntil = 0;
  voiceWeatherResultSeen = false;
}

void finishVoiceListening(unsigned long now, bool applyLockout) {
  voiceMode = VoiceMode::WaitingWakeWord;
  voiceCommandClassifierEnabled = false;
  voiceCommandAcceptsAt = 0;
  voiceCommandUntil = 0;
  if (applyLockout) {
    voiceLockoutUntil = now + config::VOICE_COMMAND_LOCKOUT_MS;
  }
  voiceDetector.setCommandMode(false);
}

void cancelVoiceListening(const char* reason) {
  if (voiceMode != VoiceMode::ListeningCommand) return;
  const unsigned long now = millis();
  Serial.printf("VOICE: COMMAND WINDOW CANCELLED reason=%s\n", reason);
  finishVoiceListening(now, false);
  if (reactionSource == ReactionSource::VoiceCommand &&
      reactionState == RobotState::Attention) {
    reactionUntil = 0;
    reactionSource = ReactionSource::None;
    suppressMicrophoneReactionAudio = false;
  }
}

void startVoiceListening(unsigned long now) {
  if (voiceBlockedByMotion()) {
    Serial.println("VOICE: WAKE ignored by higher-priority motion");
    return;
  }
  soundDetector.cancelSoundEpisode();
  voiceMode = VoiceMode::ListeningCommand;
  voiceCommandClassifierEnabled = false;
  voiceCommandAcceptsAt = now + config::VOICE_COMMAND_GUARD_MS;
  voiceCommandUntil = voiceCommandAcceptsAt + config::VOICE_COMMAND_WINDOW_MS;
  if (currentState == RobotState::Drowsy || currentState == RobotState::Sleep) {
    Serial.println("VOICE: MODE LISTENING_COMMAND visual=HIDDEN_ASLEEP");
  } else {
    showFaceFromVoice(now);
    startReaction(RobotState::Attention, now,
                  config::VOICE_COMMAND_GUARD_MS +
                      config::VOICE_COMMAND_WINDOW_MS,
                  ReactionSource::VoiceCommand);
    Serial.println("VOICE: MODE LISTENING_COMMAND visual=ATTENTION");
  }
}

void executeVoiceCommand(VoiceCommand command, unsigned long now) {
  soundDetector.cancelSoundEpisode();
  finishVoiceListening(now, true);
  reactionUntil = 0;
  reactionSource = ReactionSource::None;
  suppressMicrophoneReactionAudio = false;
  microphoneReactionWasDisplayed = false;
  manualDanceUntil = 0;
  Serial.printf("VOICE: EXECUTE %s\n", voiceCommandName(command));

  switch (command) {
    case VoiceCommand::Hello:
      showFaceFromVoice(now);
      startReaction(RobotState::Greeting, now,
                    config::GREETING_DURATION_MS,
                    ReactionSource::VoiceCommand);
      if (sound.playCue(InterfaceCue::Hello)) {
        Serial.println("VOICE: COMMAND CUE hello");
      }
      break;
    case VoiceCommand::Dance:
      showFaceFromVoice(now);
      manualDanceUntil = now + config::VOICE_DANCE_DURATION_MS;
      break;
    case VoiceCommand::Time:
      display.startScreenTransition(false, now);
      displayMode = DisplayMode::Clock;
      clockNeedsRedraw = true;
      voiceScreenUntil = now + config::VOICE_INFO_SCREEN_DURATION_MS;
      voiceScreenHardUntil = voiceScreenUntil;
      voiceTimeCuePending = true;
      break;
    case VoiceCommand::Weather:
      display.startScreenTransition(false, now);
      displayMode = DisplayMode::Weather;
      weatherNeedsRedraw = true;
      voiceWeatherResultSeen = false;
      voiceWeatherCuePending = true;
      voiceWeatherRequestObserved = false;
      voiceScreenUntil = 0;
      voiceScreenHardUntil =
          now + config::VOICE_WEATHER_TOTAL_TIMEOUT_MS;
      weatherService.requestRefresh(now);
      break;
    case VoiceCommand::Sleep:
      showFaceFromVoice(now);
      resetPresenceBehavior(now, false);
      currentState = RobotState::Drowsy;
      drowsyStartedAt = now;
      manualSleepActive = true;
      automaticSleepActive = false;
      soundDetector.cancelSoundEpisode();
      Serial.println("SLEEP: MODE MANUAL");
      Serial.println("STATE: -> DROWSY (voice)");
      break;
    case VoiceCommand::WakeUp:
      showFaceFromVoice(now);
      if (currentState == RobotState::Drowsy ||
          currentState == RobotState::Sleep) {
        beginWakeSequence(now, "STATE: -> WAKE (voice)");
      } else if (currentState != RobotState::Wake) {
        startReaction(RobotState::Surprised, now,
                      config::SURPRISED_DURATION_MS,
                      ReactionSource::VoiceCommand);
      }
      break;
    case VoiceCommand::None:
    case VoiceCommand::WakeWord:
      break;
  }
}

void handleVoiceRecognition(const VoiceRecognition& recognition,
                            unsigned long now) {
  if (recognition.command == VoiceCommand::None) return;

  if (voiceMode == VoiceMode::WaitingWakeWord) {
    if (recognition.command != VoiceCommand::WakeWord ||
        deadlineIsActive(now, voiceLockoutUntil)) {
      return;
    }
    startVoiceListening(now);
    return;
  }

  if (recognition.command == VoiceCommand::WakeWord) {
    voiceCommandUntil = now + config::VOICE_COMMAND_WINDOW_MS;
    Serial.println("VOICE: COMMAND WINDOW EXTENDED");
    return;
  }
  if (static_cast<long>(now - voiceCommandAcceptsAt) < 0) {
    Serial.printf("VOICE: IGNORED %s reason=GUARD_INTERVAL\n",
                  voiceCommandName(recognition.command));
    return;
  }
  if (voiceBlockedByMotion()) {
    cancelVoiceListening("ACTIVE_MOTION");
    return;
  }
  if (sleepStateIsActive() && recognition.command != VoiceCommand::WakeUp) {
    Serial.printf("VOICE: IGNORED %s reason=ROBOT_ASLEEP\n",
                  voiceCommandName(recognition.command));
    finishVoiceListening(now, true);
    return;
  }
  executeVoiceCommand(recognition.command, now);
}

void updateVoiceInterface(unsigned long now) {
  if (voiceMode != VoiceMode::ListeningCommand) return;
  if (voiceBlockedByMotion()) {
    cancelVoiceListening("ACTIVE_MOTION");
    return;
  }
  if (!voiceCommandClassifierEnabled &&
      static_cast<long>(now - voiceCommandAcceptsAt) >= 0) {
    voiceCommandClassifierEnabled = true;
    voiceDetector.setCommandMode(true);
  }
  if (!deadlineIsActive(now, voiceCommandUntil)) {
    Serial.println("VOICE: COMMAND TIMEOUT");
    finishVoiceListening(now, true);
    if (!sleepStateIsActive()) {
      startReaction(RobotState::Surprised, now,
                    config::VOICE_TIMEOUT_REACTION_MS,
                    ReactionSource::VoiceCommand);
    }
  }
}

void updateVoiceScreen(unsigned long now) {
  if (displayMode == DisplayMode::Weather && voiceScreenHardUntil != 0 &&
      !voiceWeatherResultSeen) {
    const WeatherStatus status = weatherService.status();
    if (status == WeatherStatus::Loading) {
      voiceWeatherRequestObserved = true;
    }
    if (status == WeatherStatus::Ready || status == WeatherStatus::Stale ||
        status == WeatherStatus::Error) {
      voiceWeatherResultSeen = true;
      voiceScreenUntil = now + config::VOICE_INFO_SCREEN_DURATION_MS;
      if (static_cast<long>(voiceScreenUntil - voiceScreenHardUntil) > 0) {
        voiceScreenUntil = voiceScreenHardUntil;
      }
      if (status == WeatherStatus::Ready && voiceWeatherRequestObserved &&
          voiceWeatherCuePending) {
        if (sound.playCue(InterfaceCue::Done)) {
          Serial.println("VOICE: COMMAND CUE done source=weather");
        }
        voiceWeatherCuePending = false;
      } else if (status != WeatherStatus::Ready) {
        voiceWeatherCuePending = false;
      }
    }
  }
  const unsigned long deadline =
      voiceScreenUntil != 0 ? voiceScreenUntil : voiceScreenHardUntil;
  if (deadline != 0 && !deadlineIsActive(now, deadline)) {
    display.startScreenTransition(true, now);
    displayMode = DisplayMode::Face;
    voiceScreenUntil = 0;
    voiceScreenHardUntil = 0;
    voiceWeatherResultSeen = false;
    voiceWeatherCuePending = false;
    voiceWeatherRequestObserved = false;
    Serial.println("VOICE: INFO SCREEN -> FACE");
  }
}

void updateLightSensor(unsigned long now) {
  float lightLux = 0.0F;
  if (!sensors.readLight(lightLux)) {
    lightMonitor.markInvalid();
    if (lightReadHealthy) {
      Serial.println("BH1750: read error");
      lightReadHealthy = false;
    }
    return;
  }
  if (!lightReadHealthy) {
    Serial.println("BH1750: read recovered");
    lightReadHealthy = true;
  }

  lightMonitor.addSample(lightLux, now);
  const LightSnapshot& lighting = lightMonitor.snapshot();
  if (lightMonitor.calibrationActive()) {
    Serial.printf("LIGHT_CSV,%lu,%.2f,%.2f,%.2f,%.3f\n", now,
                  lighting.rawLux, lighting.filteredLux,
                  lighting.baselineLux, lighting.relativeDrop);
  }

  if (config::SENSOR_TELEMETRY_DEBUG) {
    Serial.printf("Light: %.1f lx\n", lightLux);
  }

  // Lighting automation keeps observing BH1750 while motion/presence behavior
  // is paused. Only the legacy sleep state machine is paused here.
  if (presenceUpdatesPaused(now)) return;

  if (lightLux < config::SLEEP_LUX) {
    if (darkSince == 0) {
      darkSince = now;
      autoSleepPendingLogged = false;
    }
  } else {
    darkSince = 0;
    autoSleepPendingLogged = false;
    presenceDeferLogged = false;
  }

  if (currentState == RobotState::Drowsy) {
    if (!manualSleepActive && lightLux > config::WAKE_LUX) {
      beginWakeSequence(now, "STATE: DROWSY -> WAKE (light)");
    } else if (now - drowsyStartedAt >= config::DROWSY_DURATION_MS) {
      resetPresenceBehavior(now, true);
      currentState = RobotState::Sleep;
      Serial.println("STATE: DROWSY -> SLEEP");
    }
    return;
  }

  const bool darkLongEnough =
      darkSince != 0 && now - darkSince >= config::AUTO_SLEEP_DARK_DELAY_MS;
  const bool absentLongEnough = absenceSince != 0 &&
      now - absenceSince >= config::AUTO_SLEEP_ABSENCE_DELAY_MS;
  const bool higherPriorityActive =
      voiceMode == VoiceMode::ListeningCommand ||
      deadlineIsActive(now, reactionUntil) || voiceBlockedByMotion() ||
      deadlineIsActive(now, manualDanceUntil) ||
      gazeTracker.measurementRoute() == GazeMeasurementRoute::TrackerOnly;
  if (currentState != RobotState::Sleep && currentState != RobotState::Wake &&
      !deadlineIsActive(now, awakeUntil) && darkLongEnough &&
      absentLongEnough && !higherPriorityActive) {
    resetPresenceBehavior(now, false);
    currentState = RobotState::Drowsy;
    drowsyStartedAt = now;
    automaticSleepActive = true;
    manualSleepActive = false;
    reactionUntil = 0;
    reactionSource = ReactionSource::None;
    suppressMicrophoneReactionAudio = false;
    microphoneReactionWasDisplayed = false;
    soundDetector.cancelSoundEpisode();
    Serial.println("SLEEP: MODE AUTO");
    Serial.println("STATE: -> DROWSY");
    return;
  } else if (darkSince != 0 && !darkLongEnough && !autoSleepPendingLogged) {
    autoSleepPendingLogged = true;
    Serial.println("SLEEP: AUTO PENDING reason=DARK");
  }

  if (currentState == RobotState::Sleep && !manualSleepActive &&
      lightLux > config::WAKE_LUX) {
    beginWakeSequence(now, "STATE: SLEEP -> WAKE (light)");
  }
}

void updateDistanceSensor(unsigned long now) {
  if (currentState == RobotState::Sleep || currentState == RobotState::Wake ||
      (currentState == RobotState::Drowsy && manualSleepActive) ||
      presenceUpdatesPaused(now)) {
    return;
  }

  uint16_t distanceMm = 0;
  uint8_t rangeStatus = 255;
  const bool sensorValid = sensors.readDistance(distanceMm, rangeStatus);
  const bool valid =
      sensorValid && distanceMm >= config::DISTANCE_MIN_VALID_MM;
  const unsigned long measuredAt = millis();

  if (config::SENSOR_TELEMETRY_DEBUG &&
      now - lastDistanceLog >= config::DISTANCE_LOG_INTERVAL_MS) {
    lastDistanceLog = now;
    if (valid) {
      Serial.printf("ToF: %u mm status=%u\n", distanceMm, rangeStatus);
    } else if (sensorValid) {
      Serial.printf("ToF: rejected %u mm status=%u reason=TOO_CLOSE\n",
                    distanceMm, rangeStatus);
    } else {
      Serial.printf("ToF: invalid status=%u\n", rangeStatus);
    }
  }

  const GazeMeasurementRoute route = gazeTracker.measurementRoute();
  gazeTracker.onDistanceMeasurement(
      {measuredAt, distanceMm, rangeStatus, valid});

  if (route == GazeMeasurementRoute::Legacy) {
    const bool preserveAutomaticDrowsy =
        automaticSleepActive && currentState == RobotState::Drowsy;
    applyBehaviorUpdate(
        boredomBehavior.updateMeasurement(valid, distanceMm, measuredAt));
    if (preserveAutomaticDrowsy && automaticSleepActive) {
      currentState = RobotState::Drowsy;
    }
  } else if (route == GazeMeasurementRoute::TargetDirection && valid &&
             distanceMm <= config::GAZE_TARGET_MAX_DISTANCE_MM) {
    // A valid sample along the locked optical axis is an explicit "present"
    // observation. Empty/invalid samples remain unknown until GazeTracker has
    // completed its local reacquisition sequence.
    applyBehaviorUpdate(boredomBehavior.confirmPresence(measuredAt));
  }
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
    if (motionReading.putDown || motionReading.pickedUp ||
        motionReading.shaken || motionReading.movementStarted ||
        motionReading.returnedUpright) {
      soundDetector.cancelSoundEpisode();
    }
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

  if (config::SENSOR_TELEMETRY_DEBUG &&
      now - lastImuLog >= config::IMU_LOG_INTERVAL_MS) {
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

void beginAudioSubsystems() {
  if (config::AUDIO_ENABLED) sound.begin();
  const bool captureReady = audioCapture.begin();
  const bool soundReady = captureReady && config::SOUND_DETECTOR_ENABLED &&
                          soundDetector.begin(audioCapture);
  if (config::SOUND_DETECTOR_ENABLED) {
    Serial.printf("AUDIO: SOUND/CLAP %s\n", soundReady ? "ready" : "failed");
  }
  const bool musicReady = captureReady && config::MUSIC_ANALYZER_ENABLED &&
                          musicAnalyzer.begin(audioCapture);
  if (config::MUSIC_ANALYZER_ENABLED) {
    Serial.printf("MUSIC: analyzer %s\n", musicReady ? "ready" : "failed");
  }
  const bool voiceReady = captureReady && config::VOICE_ENABLED &&
                          voiceDetector.begin(audioCapture);
  if (!voiceReady && config::VOICE_ENABLED) {
    Serial.println("VOICE: unavailable; other audio remains active");
  }
}

void printDatasetRecorderReady() {
  Serial.printf(
      "DATASET: READY rate=%lu samples=%u format=s16le command=REC\n",
      static_cast<unsigned long>(config::AUDIO_INPUT_SAMPLE_RATE),
      static_cast<unsigned>(config::DATASET_RECORD_SAMPLES));
}

void beginDatasetRecorder() {
  datasetRecordBuffer = static_cast<int16_t*>(heap_caps_malloc(
      config::DATASET_RECORD_SAMPLES * sizeof(int16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (datasetRecordBuffer == nullptr) {
    Serial.println("DATASET: ERROR allocation_failed");
    return;
  }
  printDatasetRecorderReady();
}

void serviceDatasetRecorder() {
  if (datasetRecordBuffer == nullptr) {
    delay(50);
    return;
  }

  while (!datasetRecording && Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      datasetCommand[datasetCommandLength] = '\0';
      if (strcmp(datasetCommand, "REC") == 0) {
        datasetRecordEndSample =
            audioCapture.totalSamples() + config::DATASET_RECORD_SAMPLES;
        datasetRecording = true;
        Serial.printf("DATASET: ARMED end=%llu\n",
                      static_cast<unsigned long long>(datasetRecordEndSample));
      } else if (strcmp(datasetCommand, "STATUS") == 0) {
        printDatasetRecorderReady();
      } else if (datasetCommandLength != 0) {
        Serial.println("DATASET: ERROR expected_REC_or_STATUS");
      }
      datasetCommandLength = 0;
      continue;
    }
    if (datasetCommandLength + 1 < sizeof(datasetCommand)) {
      datasetCommand[datasetCommandLength++] = value;
    } else {
      datasetCommandLength = 0;
    }
  }

  if (!datasetRecording ||
      audioCapture.totalSamples() < datasetRecordEndSample) {
    delay(1);
    return;
  }

  if (!audioCapture.copyWindowEndingAt(datasetRecordBuffer,
                                        config::DATASET_RECORD_SAMPLES,
                                        datasetRecordEndSample)) {
    Serial.println("DATASET: ERROR window_unavailable");
    datasetRecording = false;
    return;
  }

  const size_t byteCount =
      config::DATASET_RECORD_SAMPLES * sizeof(datasetRecordBuffer[0]);
  Serial.printf("PCM_BEGIN samples=%u rate=%lu bytes=%u\n",
                static_cast<unsigned>(config::DATASET_RECORD_SAMPLES),
                static_cast<unsigned long>(config::AUDIO_INPUT_SAMPLE_RATE),
                static_cast<unsigned>(byteCount));
  Serial.write(reinterpret_cast<const uint8_t*>(datasetRecordBuffer),
               byteCount);
  Serial.print("PCM_END\n");
  Serial.flush();
  datasetRecording = false;
  printDatasetRecorderReady();
}

void serviceSmartLighting(unsigned long now) {
  tuyaPlug.update(now);
  TuyaResult result;
  while (tuyaPlug.takeResult(result)) {
    lightingAutomation.onPlugResult(result.success, result.relayKnown,
                                    result.relayOn, now);
    if (result.success) {
      Serial.printf("[TUYA] Relay state: %s (verified)\n",
                    result.relayOn ? "ON" : "OFF");
      if (result.request == TuyaRequestKind::TurnOn) {
        Serial.println(tuyaTurnOnWasAutomatic
                           ? "[AUTO] Garland activated"
                           : "[TUYA] Manual ON verified");
        if (tuyaTurnOnWasAutomatic && result.relayKnown && result.relayOn) {
          display.showLightingAnnouncement(now);
          clockNeedsRedraw = true;
          weatherNeedsRedraw = true;
        }
        tuyaTurnOnWasAutomatic = false;
      }
    } else {
      Serial.printf("[TUYA] ERROR request=%u detail=%s\n",
                    static_cast<unsigned>(result.request), result.error);
    }
  }

  const unsigned long statusInterval = tuyaPlug.relayKnown()
                                           ? config::TUYA_STATUS_INTERVAL_MS
                                           : config::TUYA_STATUS_RETRY_MS;
  if (tuyaPlug.configured() && WiFi.status() == WL_CONNECTED &&
      !tuyaPlug.busy() &&
      (lastPlugStatusAttempt == 0 ||
       now - lastPlugStatusAttempt >= statusInterval)) {
    lastPlugStatusAttempt = now;
    if (tuyaPlug.requestStatus()) {
      Serial.println("[TUYA] Periodic STATUS queued");
    }
  }

  tm localTime = {};
  const bool timeValid = timeService.getLocalTime(localTime);
  if (lightingAutomation.update(now, lightMonitor.snapshot(), timeValid,
                                timeValid ? localTime.tm_hour : 0,
                                tuyaPlug.relayKnown(), tuyaPlug.relayOn(),
                                tuyaPlug.busy())) {
    Serial.printf("[AUTO] Reason: %s\n",
                  LightingAutomation::reasonName(lightingAutomation.reason()));
    if (tuyaPlug.requestRelay(true)) {
      tuyaTurnOnWasAutomatic = true;
      Serial.println("[TUYA] Sending ON");
    } else {
      Serial.println("[TUYA] ON not queued");
      lightingAutomation.onPlugResult(false, tuyaPlug.relayKnown(),
                                      tuyaPlug.relayOn(), now);
    }
  }

  const LightingAutomationState state = lightingAutomation.state();
  if (state != lastLightingState) {
    lastLightingState = state;
    Serial.printf("[AUTO] State: %s\n",
                  LightingAutomation::stateName(state));
    if (state == LightingAutomationState::DarkeningCandidate) {
      Serial.println("[LIGHT] Darkening candidate detected");
    }
  }
  if (state == LightingAutomationState::WaitingConfirmation &&
      now - lastLightingDiagnostic >= config::LIGHT_DIAGNOSTIC_INTERVAL_MS) {
    lastLightingDiagnostic = now;
    Serial.printf("[LIGHT] Confirmation: %lu/%lu sec\n",
                  lightingAutomation.confirmationElapsedMs(now) / 1000,
                  config::LIGHT_CONFIRMATION_MS / 1000);
  }
}

void setup() {
  Serial.begin(config::AUDIO_TEST_MODE == config::AudioTestMode::DatasetRecorder
                   ? config::DATASET_SERIAL_BAUD
                   : 115200);
  delay(500);
  Serial.println("\nDESKBOT EMOTION SYSTEM v0.10");

  if (config::AUDIO_ISOLATED_TEST) {
    const char* mode =
        config::AUDIO_TEST_MODE == config::AudioTestMode::SoundEvents
            ? "SOUND_EVENTS"
        : config::AUDIO_TEST_MODE == config::AudioTestMode::MusicEvents
            ? "MUSIC_EVENTS"
        : config::AUDIO_TEST_MODE == config::AudioTestMode::DatasetRecorder
            ? "DATASET_RECORDER"
            : "VOICE_EVENTS";
    Serial.printf("AUDIO_TEST: %s (event-driven, isolated)\n", mode);
    beginAudioSubsystems();
    if (config::AUDIO_TEST_MODE == config::AudioTestMode::DatasetRecorder) {
      beginDatasetRecorder();
    }
    return;
  }

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

  if (config::IMU_ENABLED) {
    Serial.println("Keep DeskBot still: calibrating LSM6DS3...");
    motionSensorAvailable = motionSensor.begin();
    if (motionSensorAvailable) {
      Serial.println("LSM6DS3 initialized at 0x6B");
    } else {
      Serial.println("LSM6DS3 NOT FOUND at 0x6B");
    }
  } else {
    motionSensorAvailable = false;
    Serial.println("LSM6DS3 disabled by configuration");
  }

  Serial.println("OLED: 0x3C");
  Serial.println("BH1750: 0x23");
  Serial.println("VL53L0X: 0x29");
  Serial.println(config::IMU_ENABLED ? "LSM6DS3: 0x6B"
                                     : "LSM6DS3: DISABLED");
  Serial.println("APDS-9960: 0x39");
  Serial.printf("PRESENCE THRESHOLDS: near<=%u mm far>=%u mm\n",
                config::HAPPY_DISTANCE_MM,
                config::CURIOUS_DISTANCE_MM);
  Serial.printf("VL53L0X MODE: %s\n",
                config::TOF_LONG_RANGE_MODE ? "LONG_RANGE" : "DEFAULT");

  beginAudioSubsystems();

  if (neck.begin()) {
    Serial.printf(
        "NECK: ready yaw=GPIO%u pitch=GPIO%u limits=%.1f..%.1f/%.1f..%.1f\n",
        config::NECK_YAW_PIN, config::NECK_PITCH_PIN,
        config::NECK_YAW_MIN_DEG, config::NECK_YAW_MAX_DEG,
        config::NECK_PITCH_MIN_DEG, config::NECK_PITCH_MAX_DEG);
  } else {
    Serial.printf("NECK: initialization failed error=%s\n", neck.lastError());
  }

  const unsigned long now = millis();
  neckBehavior.begin(neck, now);
  gazeTracker.begin(now);
  timeService.begin(now);
  weatherService.begin(now);
  tuyaPlug.begin();
  lightingAutomation.begin(config::LIGHT_AUTOMATION_ENABLED_BY_DEFAULT);
  beginWakeSequence(now, "STATE: STARTUP -> WAKE");
}

void loop() {
  if (config::AUDIO_TEST_MODE == config::AudioTestMode::DatasetRecorder) {
    serviceDatasetRecorder();
    return;
  }
  const unsigned long now = millis();

  // Keep sampling continuously. Suppression only clears classifier state and
  // prevents the MAX98357A speaker from creating microphone events.
  bool speakerPlaying = sound.isPlaying();
  if (speakerWasPlaying && !speakerPlaying) {
    microphoneAudioGuardUntil = now + config::MIC_SELF_AUDIO_GUARD_MS;
  }
  speakerWasPlaying = speakerPlaying;
  const bool suppressInputEvents =
      speakerPlaying || deadlineIsActive(now, microphoneAudioGuardUntil);
  soundDetector.setSuppressed(suppressInputEvents);
  musicAnalyzer.setSuppressed(suppressInputEvents);
  voiceDetector.setSuppressed(suppressInputEvents);
  SoundEvent microphoneEvent = soundDetector.takeEvent();
  VoiceRecognition voiceRecognition = voiceDetector.takeRecognition();
  musicSnapshot = musicAnalyzer.snapshot();
  if (config::AUDIO_PERFORMANCE_DEBUG &&
      now - lastAudioHealthLog >= 10000) {
    const unsigned long healthIntervalMs =
        lastAudioHealthLog == 0 ? 10000 : now - lastAudioHealthLog;
    lastAudioHealthLog = now;
    const AudioCaptureStats audioStats = audioCapture.stats();
    const SoundMetrics soundMetrics = soundDetector.metrics();
    const VoiceDetectorStats voiceStats = voiceDetector.stats();
    const SoundSystemStats outputStats = sound.stats();
    const int64_t captureTimeUs =
        audioStats.lastFrameEndedAtUs - audioStats.firstFrameEndedAtUs;
    const uint64_t timedSamples =
        audioStats.capturedSamples >= config::AUDIO_CAPTURE_FRAME_SAMPLES
            ? audioStats.capturedSamples - config::AUDIO_CAPTURE_FRAME_SAMPLES
            : 0;
    const double measuredHz = captureTimeUs > 0
                                  ? timedSamples * 1000000.0 / captureTimeUs
                                  : 0.0;
    const double intervalUs = healthIntervalMs * 1000.0;
    const double soundCpu =
        100.0 * (soundMetrics.busyTimeUs - previousSoundBusyUs) / intervalUs;
    const double captureCpu =
        100.0 * (audioStats.busyTimeUs - previousCaptureBusyUs) / intervalUs;
    const double musicCpu =
        100.0 * (musicSnapshot.busyTimeUs - previousMusicBusyUs) / intervalUs;
    const double voiceCpu =
        100.0 * (voiceStats.busyTimeUs - previousVoiceBusyUs) / intervalUs;
    const double outputCpu =
        100.0 * (outputStats.busyTimeUs - previousOutputBusyUs) / intervalUs;
    const double fftRate =
        1000.0 * (musicSnapshot.fftFrames - previousMusicFftFrames) /
        healthIntervalMs;
    const double inferenceRate =
        1000.0 * (voiceStats.inferenceCount - previousVoiceInferences) /
        healthIntervalMs;
    previousSoundBusyUs = soundMetrics.busyTimeUs;
    previousMusicBusyUs = musicSnapshot.busyTimeUs;
    previousVoiceBusyUs = voiceStats.busyTimeUs;
    previousCaptureBusyUs = audioStats.busyTimeUs;
    previousOutputBusyUs = outputStats.busyTimeUs;
    previousMusicFftFrames = musicSnapshot.fftFrames;
    previousVoiceInferences = voiceStats.inferenceCount;
    Serial.printf(
        "AUDIO_HEALTH: frames=%lu samples=%llu rate=%.1fHz partial=%lu read_errors=%lu period_us=%lu..%lu\n",
        static_cast<unsigned long>(audioStats.capturedFrames),
        static_cast<unsigned long long>(audioStats.capturedSamples), measuredHz,
        static_cast<unsigned long>(audioStats.partialReads),
        static_cast<unsigned long>(audioStats.readErrors),
        static_cast<unsigned long>(audioStats.minFramePeriodUs),
        static_cast<unsigned long>(audioStats.maxFramePeriodUs));
    Serial.printf(
        "AUDIO_QUEUES: sound_drop=%lu music_drop=%lu music_gap=%lu highwater=%lu/%lu capacity=%u\n",
        static_cast<unsigned long>(audioStats.soundDroppedFrames),
        static_cast<unsigned long>(audioStats.musicDroppedFrames),
        static_cast<unsigned long>(musicSnapshot.droppedFrames),
        static_cast<unsigned long>(audioStats.soundQueueHighWater),
        static_cast<unsigned long>(audioStats.musicQueueHighWater),
        config::AUDIO_CAPTURE_QUEUE_FRAMES);
    Serial.printf(
        "AUDIO_TASKS: capture=c%d/p%u/stack%u/cpu%.1f%%/max%luus sound=c%d/p%u/stack%u/cpu%.1f%% music=c%d/p%u/stack%u/cpu%.1f%% output=c%d/p%u/stack%u/cpu%.1f%%\n",
        audioStats.taskCore, config::AUDIO_CAPTURE_TASK_PRIORITY,
        static_cast<unsigned>(audioStats.stackHighWaterMark), captureCpu,
        static_cast<unsigned long>(audioStats.maxFrameWorkUs),
        soundMetrics.taskCore, config::SOUND_TASK_PRIORITY,
        static_cast<unsigned>(soundMetrics.stackHighWaterMark), soundCpu,
        musicSnapshot.taskCore, config::MUSIC_TASK_PRIORITY,
        static_cast<unsigned>(musicSnapshot.stackHighWaterMark), musicCpu,
        outputStats.taskCore, config::AUDIO_TASK_PRIORITY,
        static_cast<unsigned>(outputStats.stackHighWaterMark), outputCpu);
    Serial.printf(
        "MUSIC_PERF: input=%lu fft=%lu rate=%.2f/s gap=%lu max_fft=%luus\n",
        static_cast<unsigned long>(musicSnapshot.inputFrames),
        static_cast<unsigned long>(musicSnapshot.fftFrames), fftRate,
        static_cast<unsigned long>(musicSnapshot.droppedFrames),
        static_cast<unsigned long>(musicSnapshot.maxFftUs));
    Serial.printf(
        "VOICE_PERF: core=%d priority=%u stack=%u count=%lu rate=%.2f/s cpu=%.1f%% DSP=%lums NN=%lums max=%luus missed=%lu\n",
        voiceStats.taskCore, config::VOICE_TASK_PRIORITY,
        static_cast<unsigned>(voiceStats.stackHighWaterMark),
        static_cast<unsigned long>(voiceStats.inferenceCount), inferenceRate,
        voiceCpu, static_cast<unsigned long>(voiceStats.lastDspMs),
        static_cast<unsigned long>(voiceStats.lastNnMs),
        static_cast<unsigned long>(voiceStats.maxInferenceUs),
        static_cast<unsigned long>(voiceStats.droppedWindows));
  }
  if ((config::MUSIC_DEBUG || config::AUDIO_PERFORMANCE_DEBUG) &&
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
    soundDetector.setSuppressed(true);
    musicAnalyzer.setSuppressed(true);
    voiceDetector.setSuppressed(true);
    microphoneEvent = SoundEvent::None;
    voiceRecognition = {};
  }

  if (musicSnapshot.active != musicWasActive) {
    musicWasActive = musicSnapshot.active;
    if (musicWasActive) {
      soundDetector.cancelSoundEpisode();
      musicVisualUntil = 0;
      Serial.printf("MUSIC: START bpm=%u confidence=%.2f\n",
                    musicSnapshot.bpm, musicSnapshot.confidence);
    } else {
      musicVisualUntil = now + config::MUSIC_EYE_SWAY_FADE_MS;
      Serial.println("MUSIC: STOP");
    }
  }

  if (config::AUDIO_ISOLATED_TEST) {
    // SoundEvents is reported directly by SoundDetector. Do not feed it into
    // the normal behavior state machine: a diagnostic event must not start a
    // reaction or affect classification of the following microphone event.
    if (config::AUDIO_TEST_MODE == config::AudioTestMode::VoiceEvents) {
      if (voiceRecognition.command == VoiceCommand::WakeWord) {
        voiceDetector.setCommandMode(true);
        voiceTestCommandUntil = now + config::VOICE_COMMAND_WINDOW_MS;
        Serial.printf("VOICE_TEST: COMMAND_WINDOW timeout=%lums\n",
                      config::VOICE_COMMAND_WINDOW_MS);
      } else if (voiceRecognition.command != VoiceCommand::None) {
        Serial.printf("VOICE_TEST: COMMAND=%s confidence=%.2f\n",
                      voiceCommandName(voiceRecognition.command),
                      voiceRecognition.confidence);
        voiceDetector.setCommandMode(false);
        voiceTestCommandUntil = 0;
      }
      if (voiceTestCommandUntil != 0 &&
          !deadlineIsActive(now, voiceTestCommandUntil)) {
        Serial.println("VOICE_TEST: COMMAND_TIMEOUT");
        voiceDetector.setCommandMode(false);
        voiceTestCommandUntil = 0;
      }
    }
    delay(1);
    return;
  }

  timeService.update(now);
  weatherService.update(now);
  const CommandConsoleEffects commandEffects =
      commandConsole.service(Serial, Serial, now);
  if (commandEffects.manualPlugRequest) {
    tuyaTurnOnWasAutomatic = false;
  }
  if (commandEffects.plugStatusRequest) {
    lastPlugStatusAttempt = now;
  }
  updateVoiceScreen(now);

  if (now - lastGestureUpdate >= config::GESTURE_INTERVAL_MS) {
    lastGestureUpdate = now;
    handleGesture(gestureSensor.read());
  }

  if (now - lastLightUpdate >= config::LIGHT_INTERVAL_MS) {
    lastLightUpdate = now;
    updateLightSensor(now);
  }
  serviceSmartLighting(now);

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

  handleVoiceRecognition(voiceRecognition, now);
  updateVoiceInterface(now);
  // Voice has priority over an ordinary sound completion delivered in the
  // same loop iteration.
  if (voiceRecognition.command == VoiceCommand::None) {
    handleMicrophoneEvent(microphoneEvent, now);
  }

  const RobotState neckState = stateForDisplay(now);
  const bool informationScreenActive = displayMode != DisplayMode::Face;
  const bool gazeMayControl =
      neck.isAvailable() && neckBehavior.enabled() &&
      !neckBehavior.manualOverrideActive(now) &&
      !informationScreenActive &&
      gazeStateAllowsControl(neckState);
  gazeTracker.setBoredSearch(neckState == RobotState::Bored, now);
  gazeTracker.setPaused(!gazeMayControl, now);
  gazeTracker.update(now, neck.yaw(), neck.isMoving());

  const GazeMeasurementRoute gazeRoute = gazeTracker.measurementRoute();
  if (gazeRoute == GazeMeasurementRoute::TrackerOnly) {
    boredomBehavior.pause(now);
  } else if (!presenceUpdatesPaused(now)) {
    boredomBehavior.resume(now);
  }

  GazePresenceEvent gazeEvent;
  while (gazeTracker.takePresenceEvent(gazeEvent)) {
    // Close the frozen interval before writing a new confirmed presence state;
    // otherwise a later resume would shift the freshly written timestamps.
    boredomBehavior.resume(now);
    if (gazeEvent == GazePresenceEvent::TargetConfirmed) {
      applyBehaviorUpdate(boredomBehavior.confirmPresence(now));
    } else if (gazeEvent == GazePresenceEvent::TargetLost) {
      applyBehaviorUpdate(boredomBehavior.confirmAbsence(now));
    }
  }

  neckBehavior.setGazeControlActive(gazeTracker.enabled() &&
                                    !gazeTracker.paused());
  neckBehavior.setGazeTarget(gazeTracker.hasTarget(),
                             gazeTracker.targetYaw());
  neckBehavior.update(now, neckState, musicSnapshot.beatSequence,
                      musicSnapshot.active);
  GazeRequest gazeRequest;
  if (gazeMayControl && gazeTracker.takeRequest(gazeRequest)) {
    neckBehavior.requestGaze(gazeRequest, now, neckState);
  }

  if (now - lastDisplayUpdate >= config::DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    const bool lightingAnnouncementActive =
        display.lightingAnnouncementActive(now);
    if (lightingAnnouncementActive != lightingAnnouncementWasActive) {
      clockNeedsRedraw = true;
      weatherNeedsRedraw = true;
      lightingAnnouncementWasActive = lightingAnnouncementActive;
    }
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
        if (voiceTimeCuePending) {
          if (timeValid) {
            if (sound.playCue(InterfaceCue::Done)) {
              Serial.println("VOICE: COMMAND CUE done source=time");
            }
          }
          voiceTimeCuePending = false;
        }
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
      reactionSource == ReactionSource::MicrophoneClap ||
      reactionSource == ReactionSource::VoiceCommand;
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
