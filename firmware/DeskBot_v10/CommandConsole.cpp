#include "CommandConsole.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "Config.h"
#include "GazeTracker.h"
#include "LightMonitor.h"
#include "LightingAutomation.h"
#include "NeckBehaviorController.h"
#include "NeckController.h"
#include "TuyaPlugClient.h"
#include "TuyaSecrets.h"

CommandConsole::CommandConsole(GazeTracker& gazeTracker, NeckController& neck,
                               NeckBehaviorController& neckBehavior,
                               LightMonitor& lightMonitor,
                               LightingAutomation& lightingAutomation,
                               TuyaPlugClient& tuyaPlug)
    : gazeTracker_(gazeTracker),
      neck_(neck),
      neckBehavior_(neckBehavior),
      lightMonitor_(lightMonitor),
      lightingAutomation_(lightingAutomation),
      tuyaPlug_(tuyaPlug) {}

CommandConsoleEffects CommandConsole::service(Stream& input, Print& output,
                                              uint32_t nowMs) {
  CommandConsoleEffects effects;
  while (input.available() > 0) {
    const char value = static_cast<char>(input.read());
    if (value == '\r') continue;
    if (value == '\n') {
      command_[commandLength_] = '\0';
      handleCommand(command_, output, nowMs, effects);
      commandLength_ = 0;
      continue;
    }
    if (commandLength_ + 1 < sizeof(command_)) {
      command_[commandLength_++] =
          static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    } else {
      commandLength_ = 0;
      output.println("LIGHT_CMD_ERROR: line_too_long");
    }
  }
  return effects;
}

bool CommandConsole::parseFloat(const char*& cursor, float& value) {
  while (*cursor == ' ') ++cursor;
  if (*cursor == '\0') return false;
  char* end = nullptr;
  value = std::strtof(cursor, &end);
  if (end == cursor) return false;
  cursor = end;
  return true;
}

bool CommandConsole::commandEnds(const char* cursor) {
  while (*cursor == ' ') ++cursor;
  return *cursor == '\0';
}

bool CommandConsole::angleInRange(float value, float minimum, float maximum) {
  return value >= minimum && value <= maximum;
}

void CommandConsole::printLightStatus(Print& output, uint32_t nowMs) const {
  const LightSnapshot& light = lightMonitor_.snapshot();
  output.printf(
      "[LIGHT] valid=%s lux=%.2f filtered=%.2f recent=%.2f baseline=%.2f noise=%.2f drop=%.1f%% required=%.2f darkening=%s samples=%u\n",
      light.valid ? "YES" : "NO", light.rawLux, light.filteredLux,
      light.recentLux, light.baselineLux, light.noiseLux,
      light.relativeDrop * 100.0F, light.requiredDropLux,
      light.darkening ? "YES" : "NO", light.historySamples);
  output.printf(
      "[AUTO] enabled=%s state=%s reason=%s confirm=%lu/%lu ms override=%lu ms\n",
      lightingAutomation_.enabled() ? "YES" : "NO",
      LightingAutomation::stateName(lightingAutomation_.state()),
      LightingAutomation::reasonName(lightingAutomation_.reason()),
      lightingAutomation_.confirmationElapsedMs(nowMs),
      config::LIGHT_CONFIRMATION_MS,
      lightingAutomation_.manualOverrideRemainingMs(nowMs));
}

void CommandConsole::printPlugInfo(Print& output) const {
  output.printf(
      "[TUYA] state=%s ip=%u.%u.%u.%u port=6668 protocol=3.5 dps=%s busy=%s relay=%s last_success_ms=%lu error=%s\n",
      TuyaPlugClient::stateName(tuyaPlug_.state()), TUYA_PLUG_IP[0],
      TUYA_PLUG_IP[1], TUYA_PLUG_IP[2], TUYA_PLUG_IP[3],
      config::TUYA_RELAY_DPS,
      tuyaPlug_.busy() ? "YES" : "NO",
      tuyaPlug_.relayKnown() ? (tuyaPlug_.relayOn() ? "ON" : "OFF")
                             : "UNKNOWN",
      tuyaPlug_.lastSuccessAt(),
      tuyaPlug_.lastError()[0] == '\0' ? "NONE" : tuyaPlug_.lastError());
}

void CommandConsole::printNeckStatus(Print& output, uint32_t nowMs) const {
  output.printf(
      "[NECK] available=%s attached=%s moving=%s gesture=%s yaw=%.1f->%.1f pitch=%.1f->%.1f limits_yaw=%.1f..%.1f limits_pitch=%.1f..%.1f error=%s auto=%s override_ms=%lu\n",
      neck_.isAvailable() ? "YES" : "NO",
      neck_.isAttached() ? "YES" : "NO", neck_.isMoving() ? "YES" : "NO",
      NeckController::gestureName(neck_.gesture()), neck_.yaw(),
      neck_.targetYaw(), neck_.pitch(), neck_.targetPitch(),
      config::NECK_YAW_MIN_DEG, config::NECK_YAW_MAX_DEG,
      config::NECK_PITCH_MIN_DEG, config::NECK_PITCH_MAX_DEG,
      neck_.lastError(), neckBehavior_.modeName(nowMs),
      static_cast<unsigned long>(
          neckBehavior_.manualOverrideRemainingMs(nowMs)));
}

bool CommandConsole::handleGazeCommand(const char* command, Print& output,
                                       uint32_t nowMs) {
  if (strncmp(command, "GAZE", 4) != 0) return false;

  if (strcmp(command, "GAZE STATUS") == 0) {
    gazeTracker_.printStatus(output, nowMs);
  } else if (strcmp(command, "GAZE AUTO ON") == 0) {
    gazeTracker_.setEnabled(true, nowMs);
    output.println("GAZE: automatic tracking enabled");
  } else if (strcmp(command, "GAZE AUTO OFF") == 0) {
    gazeTracker_.setEnabled(false, nowMs);
    output.println("GAZE: automatic tracking disabled");
  } else if (strcmp(command, "GAZE SCAN") == 0) {
    if (!gazeTracker_.enabled()) {
      output.println("GAZE_CMD_ERROR: automatic tracking is disabled");
    } else {
      gazeTracker_.requestScan(nowMs);
      output.println("GAZE: scan requested");
    }
  } else if (strcmp(command, "GAZE RELEASE") == 0) {
    gazeTracker_.releaseTarget(nowMs);
    output.println("GAZE: target released");
  } else if (strcmp(command, "GAZE MAP") == 0) {
    gazeTracker_.printMap(output);
  } else if (strcmp(command, "GAZE DEBUG ON") == 0) {
    gazeTracker_.setDebug(true);
    output.println("GAZE: debug enabled");
  } else if (strcmp(command, "GAZE DEBUG OFF") == 0) {
    gazeTracker_.setDebug(false);
    output.println("GAZE: debug disabled");
  } else {
    output.println(
        "GAZE_CMD_ERROR: expected STATUS, AUTO ON|OFF, SCAN, RELEASE, MAP, or DEBUG ON|OFF");
  }
  return true;
}

bool CommandConsole::handleNeckCommand(const char* command, Print& output,
                                       uint32_t nowMs) {
  if (strncmp(command, "NECK", 4) != 0) return false;

  if (strcmp(command, "NECK AUTO ON") == 0) {
    neckBehavior_.setEnabled(true, nowMs);
    output.println("[NECK] automatic behavior enabled");
  } else if (strcmp(command, "NECK AUTO OFF") == 0) {
    neckBehavior_.setEnabled(false, nowMs);
    neck_.stop();
    output.println("[NECK] automatic behavior disabled; motion stopped");
  } else if (strcmp(command, "NECK AUTO STATUS") == 0) {
    output.printf("[NECK] auto=%s override_ms=%lu\n",
                  neckBehavior_.modeName(nowMs),
                  static_cast<unsigned long>(
                      neckBehavior_.manualOverrideRemainingMs(nowMs)));
  } else if (strcmp(command, "NECK STATUS") == 0) {
    printNeckStatus(output, nowMs);
  } else if (strcmp(command, "NECK CENTER") == 0) {
    neckBehavior_.noteManualCommand(nowMs);
    output.println(neck_.center(config::NECK_DEFAULT_SPEED_DEG_PER_SEC)
                       ? "[NECK] CENTER started"
                       : "NECK_CMD_ERROR: unavailable");
  } else if (strcmp(command, "NECK STOP") == 0) {
    neckBehavior_.noteManualCommand(nowMs);
    neck_.stop();
    output.println("[NECK] stopped");
  } else if (strcmp(command, "NECK DETACH") == 0) {
    neckBehavior_.noteManualCommand(nowMs);
    neck_.detach();
    output.println("[NECK] PWM detached");
  } else if (strcmp(command, "NECK NOD") == 0) {
    neckBehavior_.noteManualCommand(nowMs);
    output.println(neck_.startGesture(NeckGesture::Nod)
                       ? "[NECK] NOD started"
                       : "NECK_CMD_ERROR: unavailable");
  } else if (strcmp(command, "NECK LOOK") == 0) {
    neckBehavior_.noteManualCommand(nowMs);
    output.println(neck_.startGesture(NeckGesture::LookAround)
                       ? "[NECK] LOOK started"
                       : "NECK_CMD_ERROR: unavailable");
  } else if (strcmp(command, "NECK LEFT") == 0 ||
             strcmp(command, "NECK RIGHT") == 0) {
    neckBehavior_.noteManualCommand(nowMs);
    const float yaw = strcmp(command, "NECK LEFT") == 0
                          ? config::NECK_YAW_MIN_DEG
                          : config::NECK_YAW_MAX_DEG;
    output.println(neck_.moveTo(yaw, config::NECK_PITCH_CENTER_DEG,
                                config::NECK_DEFAULT_SPEED_DEG_PER_SEC)
                       ? "[NECK] turn started"
                       : "NECK_CMD_ERROR: unavailable");
  } else if (strncmp(command, "NECK YAW ", 9) == 0) {
    const char* cursor = command + 9;
    float yaw = 0.0F;
    if (!parseFloat(cursor, yaw) || !commandEnds(cursor) ||
        !angleInRange(yaw, config::NECK_YAW_MIN_DEG,
                      config::NECK_YAW_MAX_DEG)) {
      output.printf("NECK_CMD_ERROR: yaw must be %.1f..%.1f\n",
                    config::NECK_YAW_MIN_DEG, config::NECK_YAW_MAX_DEG);
    } else {
      neckBehavior_.noteManualCommand(nowMs);
      output.println(neck_.moveTo(yaw, neck_.pitch(),
                                  config::NECK_DEFAULT_SPEED_DEG_PER_SEC)
                         ? "[NECK] YAW started"
                         : "NECK_CMD_ERROR: unavailable");
    }
  } else if (strncmp(command, "NECK PITCH ", 11) == 0) {
    const char* cursor = command + 11;
    float pitch = 0.0F;
    if (!parseFloat(cursor, pitch) || !commandEnds(cursor) ||
        !angleInRange(pitch, config::NECK_PITCH_MIN_DEG,
                      config::NECK_PITCH_MAX_DEG)) {
      output.printf("NECK_CMD_ERROR: pitch must be %.1f..%.1f\n",
                    config::NECK_PITCH_MIN_DEG,
                    config::NECK_PITCH_MAX_DEG);
    } else {
      neckBehavior_.noteManualCommand(nowMs);
      output.println(neck_.moveTo(neck_.yaw(), pitch,
                                  config::NECK_DEFAULT_SPEED_DEG_PER_SEC)
                         ? "[NECK] PITCH started"
                         : "NECK_CMD_ERROR: unavailable");
    }
  } else if (strncmp(command, "NECK MOVE ", 10) == 0) {
    const char* cursor = command + 10;
    float yaw = 0.0F;
    float pitch = 0.0F;
    if (!parseFloat(cursor, yaw) || !parseFloat(cursor, pitch) ||
        !commandEnds(cursor)) {
      output.println("NECK_CMD_ERROR: expected NECK MOVE <yaw> <pitch>");
    } else if (!angleInRange(yaw, config::NECK_YAW_MIN_DEG,
                             config::NECK_YAW_MAX_DEG) ||
               !angleInRange(pitch, config::NECK_PITCH_MIN_DEG,
                             config::NECK_PITCH_MAX_DEG)) {
      output.printf(
          "NECK_CMD_ERROR: yaw %.1f..%.1f, pitch %.1f..%.1f\n",
          config::NECK_YAW_MIN_DEG, config::NECK_YAW_MAX_DEG,
          config::NECK_PITCH_MIN_DEG, config::NECK_PITCH_MAX_DEG);
    } else {
      neckBehavior_.noteManualCommand(nowMs);
      output.println(neck_.moveTo(yaw, pitch,
                                  config::NECK_DEFAULT_SPEED_DEG_PER_SEC)
                         ? "[NECK] MOVE started"
                         : "NECK_CMD_ERROR: unavailable");
    }
  } else {
    output.println(
        "NECK_CMD_ERROR: expected STATUS, AUTO ON|OFF|STATUS, CENTER|LEFT|RIGHT|NOD|LOOK|STOP|DETACH, YAW <deg>, PITCH <deg>, or MOVE <yaw> <pitch>");
  }
  return true;
}

void CommandConsole::handleCommand(const char* command, Print& output,
                                   uint32_t nowMs,
                                   CommandConsoleEffects& effects) {
  if (handleGazeCommand(command, output, nowMs) ||
      handleNeckCommand(command, output, nowMs)) {
    return;
  }
  if (strcmp(command, "PLUG INFO") == 0) {
    printPlugInfo(output);
  } else if (strcmp(command, "PLUG STATUS") == 0) {
    if (tuyaPlug_.requestStatus()) {
      effects.plugStatusRequest = true;
      output.println("[TUYA] STATUS queued");
    } else {
      output.println("[TUYA] STATUS not queued");
    }
  } else if (strcmp(command, "PLUG ON") == 0 ||
             strcmp(command, "PLUG OFF") == 0) {
    const bool desired = strcmp(command, "PLUG ON") == 0;
    if (tuyaPlug_.relayKnown() && tuyaPlug_.relayOn() == desired) {
      output.printf("[TUYA] Relay already %s; command skipped\n",
                    desired ? "ON" : "OFF");
      lightingAutomation_.setManualOverride(desired, nowMs);
      effects.manualPlugRequest = true;
    } else if (tuyaPlug_.requestRelay(desired)) {
      lightingAutomation_.setManualOverride(desired, nowMs);
      effects.manualPlugRequest = true;
      output.printf("[TUYA] Sending %s (queued)\n", desired ? "ON" : "OFF");
    } else {
      output.printf("[TUYA] %s not queued: state=%s error=%s\n",
                    desired ? "ON" : "OFF",
                    TuyaPlugClient::stateName(tuyaPlug_.state()),
                    tuyaPlug_.lastError()[0] == '\0' ? "BUSY_OR_BACKOFF"
                                                     : tuyaPlug_.lastError());
    }
  } else if (strcmp(command, "LIGHT STATUS") == 0) {
    printLightStatus(output, nowMs);
  } else if (strcmp(command, "LIGHT AUTO ON") == 0) {
    lightingAutomation_.setEnabled(true, nowMs);
    output.println("[AUTO] enabled; manual override cleared");
  } else if (strcmp(command, "LIGHT AUTO OFF") == 0) {
    lightingAutomation_.setEnabled(false, nowMs);
    output.println("[AUTO] disabled; relay state unchanged");
  } else if (strcmp(command, "LIGHT CSV START") == 0) {
    lightMonitor_.startCalibration(nowMs);
    output.println(
        "LIGHT_CSV_HEADER,timestamp_ms,raw_lux,filtered_lux,baseline_lux,relative_drop");
  } else if (strcmp(command, "LIGHT CSV STOP") == 0) {
    const LightCalibrationSummary summary =
        lightMonitor_.stopCalibration(nowMs);
    if (summary.valid) {
      output.printf(
          "LIGHT_CSV_SUMMARY,samples=%u,duration_ms=%lu,average=%.2f,median=%.2f,min=%.2f,max=%.2f\n",
          summary.samples, summary.durationMs, summary.average, summary.median,
          summary.minimum, summary.maximum);
    } else {
      output.println("LIGHT_CSV_SUMMARY,no_valid_samples");
    }
  } else if (command[0] != '\0') {
    output.println(
        "LIGHT_CMD_ERROR: expected PLUG ON|OFF|STATUS|INFO, LIGHT STATUS, LIGHT AUTO ON|OFF, or LIGHT CSV START|STOP");
  }
}
