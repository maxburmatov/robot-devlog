#pragma once

#include <Arduino.h>

#include "LightMonitor.h"

enum class LightingAutomationState : uint8_t {
  AutoDisabled,
  AutoIdle,
  DarkeningCandidate,
  WaitingConfirmation,
  LightingOn,
  ManualOverride,
  ConnectionError,
};

enum class LightingReason : uint8_t {
  None,
  EveningDarkness,
  RelativeDarkening,
  Manual,
};

class LightingAutomation {
 public:
  void begin(bool enabled);
  bool update(unsigned long now, const LightSnapshot& light,
              bool localTimeValid, uint8_t localHour,
              bool relayKnown, bool relayOn, bool plugBusy);
  void setEnabled(bool enabled, unsigned long now);
  void setManualOverride(bool relayOn, unsigned long now);
  void onPlugResult(bool success, bool relayKnown, bool relayOn,
                    unsigned long now);

  bool enabled() const;
  LightingAutomationState state() const;
  LightingReason reason() const;
  unsigned long confirmationElapsedMs(unsigned long now) const;
  unsigned long manualOverrideRemainingMs(unsigned long now) const;

  static const char* stateName(LightingAutomationState state);
  static const char* reasonName(LightingReason reason);

 private:
  static bool hourInWindow(uint8_t hour, uint8_t start, uint8_t end);
  void cancelCandidate();

  bool enabled_ = true;
  LightingAutomationState state_ = LightingAutomationState::AutoIdle;
  LightingReason reason_ = LightingReason::None;
  unsigned long candidateSince_ = 0;
  unsigned long overrideUntil_ = 0;
  unsigned long retryAt_ = 0;
  bool activationQueued_ = false;
};
