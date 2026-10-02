#include "BoredomBehavior.h"

BoredomBehavior::BoredomBehavior(const BoredomConfig& config)
    : config_(config) {}

BoredomUpdate BoredomBehavior::result(PresenceEvent event,
                                      RobotState previous) const {
  BoredomUpdate update;
  update.presenceEvent = event;
  update.behaviorChanged = state_ != previous;
  update.behavior = state_;
  return update;
}

void BoredomBehavior::enterState(RobotState state, uint32_t startedAt) {
  state_ = state;
  stateStartedAt_ = startedAt;
  stateEnteredAt_ = startedAt;
  ++stateRevision_;
}

void BoredomBehavior::resetHistory(uint32_t now) {
  presence_ = PresenceStatus::Unknown;
  enterState(RobotState::Curious, now);
  absenceStartedAt_ = now;
  lastValidMeasurementAt_ = now;
  unreliableStartedAt_ = now;
  nearSamples_ = 0;
  farSamples_ = 0;
  invalidSamples_ = 0;
  invalidStartedAt_ = now;
  returnPending_ = false;
  boredomReached_ = false;
  invalidActsAsAbsence_ = false;
  measurementReliable_ = false;
}

BoredomUpdate BoredomBehavior::reset(uint32_t now) {
  const RobotState previous = state_;
  resetHistory(now);
  sensorUnavailable_ = false;
  return result(PresenceEvent::None, previous);
}

void BoredomBehavior::restoreMeasurementReliability(uint32_t now) {
  if (measurementReliable_) {
    return;
  }

  const uint32_t pausedFor = now - unreliableStartedAt_;
  stateStartedAt_ += pausedFor;
  absenceStartedAt_ += pausedFor;
  measurementReliable_ = true;
}

BoredomUpdate BoredomBehavior::updateMeasurement(bool valid,
                                                  uint16_t distanceMm,
                                                  uint32_t now) {
  const RobotState previous = state_;
  bool confirmedInvalidAbsence = false;

  if (!valid) {
    nearSamples_ = 0;
    farSamples_ = 0;

    const bool invalidMayMeanAbsence =
        presence_ == PresenceStatus::Present ||
        invalidActsAsAbsence_ ||
        (presence_ == PresenceStatus::Absent && returnPending_);

    if (invalidMayMeanAbsence) {
      if (invalidSamples_ == 0) {
        invalidStartedAt_ = now;
      }
      if (invalidSamples_ < config_.confirmationSamples) {
        ++invalidSamples_;
      }

      if (invalidSamples_ >= config_.confirmationSamples &&
          now - invalidStartedAt_ >= config_.sensorUnavailableMs) {
        confirmedInvalidAbsence = true;
        valid = true;
        distanceMm = config_.absenceDistanceMm;
        invalidActsAsAbsence_ = true;
        // Серия invalid samples уже подтвердила уход; не требуем после неё
        // ещё одну полную серию искусственных дальних измерений.
        farSamples_ = config_.confirmationSamples - 1;
      }
    } else {
      invalidSamples_ = 0;
      invalidActsAsAbsence_ = false;
    }

    if (measurementReliable_) {
      measurementReliable_ = false;
      unreliableStartedAt_ = now;
    }

    if (!confirmedInvalidAbsence && !invalidMayMeanAbsence &&
        !sensorUnavailable_ &&
        now - lastValidMeasurementAt_ >= config_.sensorUnavailableMs) {
      resetHistory(now);
      sensorUnavailable_ = true;
      return result(PresenceEvent::SensorUnavailable, previous);
    }
    if (!confirmedInvalidAbsence) {
      return result(PresenceEvent::None, previous);
    }
  }

  restoreMeasurementReliability(now);
  lastValidMeasurementAt_ = now;
  sensorUnavailable_ = false;
  if (!confirmedInvalidAbsence) {
    invalidSamples_ = 0;
    invalidActsAsAbsence_ = false;
  }
  PresenceEvent event = PresenceEvent::None;

  if (distanceMm <= config_.presenceDistanceMm) {
    farSamples_ = 0;
    if (nearSamples_ < config_.confirmationSamples) {
      ++nearSamples_;
    }

    if (nearSamples_ >= config_.confirmationSamples &&
        presence_ != PresenceStatus::Present) {
      const bool isReturn =
          presence_ == PresenceStatus::Absent && returnPending_;
      const bool offendedReturn =
          isReturn &&
          (boredomReached_ || now - absenceStartedAt_ >= config_.boredMs);
      presence_ = PresenceStatus::Present;
      nearSamples_ = 0;
      returnPending_ = false;
      boredomReached_ = false;
      enterState(offendedReturn ? RobotState::Offended : RobotState::Happy,
                 now);
      event = isReturn ? PresenceEvent::Returned : PresenceEvent::Confirmed;
    }
  } else if (distanceMm >= config_.absenceDistanceMm) {
    nearSamples_ = 0;
    if (farSamples_ < config_.confirmationSamples) {
      ++farSamples_;
    }

    if (farSamples_ >= config_.confirmationSamples &&
        presence_ != PresenceStatus::Absent) {
      const bool wasPresent = presence_ == PresenceStatus::Present;
      presence_ = PresenceStatus::Absent;
      farSamples_ = 0;
      absenceStartedAt_ = now;
      returnPending_ = wasPresent;
      boredomReached_ = false;
      enterState(RobotState::Curious, now);
      event = wasPresent ? PresenceEvent::Lost : PresenceEvent::None;
    }
  } else {
    nearSamples_ = 0;
    farSamples_ = 0;
  }

  BoredomUpdate measurementUpdate = result(event, previous);
  if (measurementUpdate.behaviorChanged || event != PresenceEvent::None) {
    return measurementUpdate;
  }
  return update(now);
}

BoredomUpdate BoredomBehavior::update(uint32_t now) {
  const RobotState previous = state_;
  if (!measurementReliable_) {
    return result(PresenceEvent::None, previous);
  }

  if (presence_ == PresenceStatus::Present) {
    if (state_ == RobotState::Offended &&
        now - stateStartedAt_ >= config_.offendedMs) {
      enterState(RobotState::Reconciling,
                 stateStartedAt_ + config_.offendedMs);
    } else if (state_ == RobotState::Reconciling &&
               now - stateStartedAt_ >= config_.reconcileMs) {
      enterState(RobotState::Content,
                 stateStartedAt_ + config_.reconcileMs);
    } else if (state_ == RobotState::Happy &&
               now - stateStartedAt_ >= config_.happyMs) {
      enterState(RobotState::Content, stateStartedAt_ + config_.happyMs);
    }
  } else if (presence_ == PresenceStatus::Absent) {
    const uint32_t absentFor = now - absenceStartedAt_;
    if (state_ == RobotState::Curious && absentFor >= config_.waitingMs) {
      enterState(RobotState::Waiting,
                 absenceStartedAt_ + config_.waitingMs);
    } else if (state_ == RobotState::Waiting &&
               absentFor >= config_.boredMs) {
      enterState(RobotState::Bored,
                 absenceStartedAt_ + config_.boredMs);
      boredomReached_ = true;
    }
  }

  return result(PresenceEvent::None, previous);
}

RobotState BoredomBehavior::state() const {
  return state_;
}

uint32_t BoredomBehavior::stateStartedAt() const {
  return stateStartedAt_;
}

uint32_t BoredomBehavior::stateEnteredAt() const {
  return stateEnteredAt_;
}

uint32_t BoredomBehavior::stateRevision() const {
  return stateRevision_;
}

bool BoredomBehavior::hasConfirmedPresence() const {
  return presence_ == PresenceStatus::Present;
}
