#include "LightingAutomation.h"

#include "Config.h"

void LightingAutomation::begin(bool enabled) {
  enabled_ = enabled;
  state_ = enabled ? LightingAutomationState::AutoIdle
                   : LightingAutomationState::AutoDisabled;
}

bool LightingAutomation::update(unsigned long now, const LightSnapshot& light,
                                bool localTimeValid, uint8_t localHour,
                                bool relayKnown, bool relayOn,
                                bool plugBusy) {
  if (!enabled_) {
    state_ = LightingAutomationState::AutoDisabled;
    return false;
  }

  if (overrideUntil_ != 0 && static_cast<int32_t>(overrideUntil_ - now) > 0) {
    state_ = LightingAutomationState::ManualOverride;
    return false;
  }
  overrideUntil_ = 0;

  if (relayKnown && relayOn) {
    cancelCandidate();
    state_ = LightingAutomationState::LightingOn;
    return false;
  }
  if (activationQueued_ || plugBusy) return false;
  if (retryAt_ != 0 && static_cast<int32_t>(retryAt_ - now) > 0) return false;

  if (!light.valid || !localTimeValid) {
    cancelCandidate();
    state_ = LightingAutomationState::AutoIdle;
    return false;
  }

  const bool evening =
      hourInWindow(localHour, config::LIGHT_EVENING_START_HOUR,
                   config::LIGHT_EVENING_END_HOUR) &&
      light.filteredLux <= config::LIGHT_EVENING_MAX_LUX;
  const bool relativeDarkening =
      hourInWindow(localHour, config::LIGHT_DAY_START_HOUR,
                   config::LIGHT_EVENING_END_HOUR) && light.darkening;
  const LightingReason newReason = evening
                                       ? LightingReason::EveningDarkness
                                       : relativeDarkening
                                             ? LightingReason::RelativeDarkening
                                             : LightingReason::None;
  if (newReason == LightingReason::None) {
    cancelCandidate();
    state_ = LightingAutomationState::AutoIdle;
    return false;
  }

  if (candidateSince_ == 0 || reason_ != newReason) {
    candidateSince_ = now == 0 ? 1 : now;
    reason_ = newReason;
    state_ = LightingAutomationState::DarkeningCandidate;
    return false;
  }

  state_ = LightingAutomationState::WaitingConfirmation;
  if (now - candidateSince_ < config::LIGHT_CONFIRMATION_MS) return false;

  activationQueued_ = true;
  return true;
}

void LightingAutomation::setEnabled(bool enabled, unsigned long now) {
  enabled_ = enabled;
  overrideUntil_ = 0;
  retryAt_ = 0;
  activationQueued_ = false;
  cancelCandidate();
  state_ = enabled ? LightingAutomationState::AutoIdle
                   : LightingAutomationState::AutoDisabled;
  (void)now;
}

void LightingAutomation::setManualOverride(bool relayOn, unsigned long now) {
  cancelCandidate();
  activationQueued_ = false;
  reason_ = LightingReason::Manual;
  overrideUntil_ = now + config::LIGHT_MANUAL_OVERRIDE_MS;
  state_ = LightingAutomationState::ManualOverride;
  (void)relayOn;
}

void LightingAutomation::onPlugResult(bool success, bool relayKnown,
                                      bool relayOn, unsigned long now) {
  activationQueued_ = false;
  if (!success) {
    state_ = LightingAutomationState::ConnectionError;
    retryAt_ = now + config::LIGHT_AUTOMATION_RETRY_MS;
    return;
  }
  retryAt_ = 0;
  if (overrideUntil_ != 0) {
    state_ = LightingAutomationState::ManualOverride;
  } else if (relayKnown && relayOn) {
    state_ = LightingAutomationState::LightingOn;
  } else {
    state_ = LightingAutomationState::AutoIdle;
  }
}

bool LightingAutomation::enabled() const { return enabled_; }
LightingAutomationState LightingAutomation::state() const { return state_; }
LightingReason LightingAutomation::reason() const { return reason_; }

unsigned long LightingAutomation::confirmationElapsedMs(unsigned long now) const {
  return candidateSince_ == 0 ? 0 : now - candidateSince_;
}

unsigned long LightingAutomation::manualOverrideRemainingMs(
    unsigned long now) const {
  return overrideUntil_ != 0 && static_cast<int32_t>(overrideUntil_ - now) > 0
             ? overrideUntil_ - now
             : 0;
}

bool LightingAutomation::hourInWindow(uint8_t hour, uint8_t start,
                                      uint8_t end) {
  return start < end ? hour >= start && hour < end
                     : hour >= start || hour < end;
}

void LightingAutomation::cancelCandidate() {
  candidateSince_ = 0;
  reason_ = LightingReason::None;
}

const char* LightingAutomation::stateName(LightingAutomationState state) {
  switch (state) {
    case LightingAutomationState::AutoDisabled: return "AUTO_DISABLED";
    case LightingAutomationState::AutoIdle: return "AUTO_IDLE";
    case LightingAutomationState::DarkeningCandidate: return "DARKENING_CANDIDATE";
    case LightingAutomationState::WaitingConfirmation: return "WAITING_CONFIRMATION";
    case LightingAutomationState::LightingOn: return "LIGHTING_ON";
    case LightingAutomationState::ManualOverride: return "MANUAL_OVERRIDE";
    case LightingAutomationState::ConnectionError: return "CONNECTION_ERROR";
  }
  return "UNKNOWN";
}

const char* LightingAutomation::reasonName(LightingReason reason) {
  switch (reason) {
    case LightingReason::None: return "NONE";
    case LightingReason::EveningDarkness: return "EVENING_DARKNESS";
    case LightingReason::RelativeDarkening: return "RELATIVE_DARKENING";
    case LightingReason::Manual: return "MANUAL";
  }
  return "UNKNOWN";
}
