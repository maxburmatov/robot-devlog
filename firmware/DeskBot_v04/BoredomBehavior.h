#pragma once

#include <stdint.h>

#include "RobotState.h"

struct BoredomConfig {
  uint16_t presenceDistanceMm;
  uint16_t absenceDistanceMm;
  uint8_t confirmationSamples;
  uint32_t sensorUnavailableMs;
  uint32_t waitingMs;
  uint32_t boredMs;
  uint32_t offendedMs;
  uint32_t reconcileMs;
  uint32_t happyMs;
};

enum class PresenceEvent {
  None,
  Confirmed,
  Lost,
  Returned,
  SensorUnavailable,
};

struct BoredomUpdate {
  PresenceEvent presenceEvent = PresenceEvent::None;
  bool behaviorChanged = false;
  RobotState behavior = RobotState::Curious;
};

class BoredomBehavior {
 public:
  explicit BoredomBehavior(const BoredomConfig& config);

  BoredomUpdate reset(uint32_t now);
  BoredomUpdate updateMeasurement(bool valid, uint16_t distanceMm,
                                   uint32_t now);
  BoredomUpdate update(uint32_t now);

  RobotState state() const;
  uint32_t stateStartedAt() const;
  uint32_t stateEnteredAt() const;
  uint32_t stateRevision() const;
  bool hasConfirmedPresence() const;

 private:
  enum class PresenceStatus {
    Unknown,
    Present,
    Absent,
  };

  BoredomConfig config_;
  PresenceStatus presence_ = PresenceStatus::Unknown;
  RobotState state_ = RobotState::Curious;
  uint32_t stateStartedAt_ = 0;
  uint32_t stateEnteredAt_ = 0;
  uint32_t stateRevision_ = 0;
  uint32_t absenceStartedAt_ = 0;
  uint32_t lastValidMeasurementAt_ = 0;
  uint32_t unreliableStartedAt_ = 0;
  uint8_t nearSamples_ = 0;
  uint8_t farSamples_ = 0;
  uint8_t invalidSamples_ = 0;
  uint32_t invalidStartedAt_ = 0;
  bool returnPending_ = false;
  bool boredomReached_ = false;
  bool invalidActsAsAbsence_ = false;
  bool measurementReliable_ = false;
  bool sensorUnavailable_ = false;

  BoredomUpdate result(PresenceEvent event, RobotState previous) const;
  void enterState(RobotState state, uint32_t startedAt);
  void resetHistory(uint32_t now);
  void restoreMeasurementReliability(uint32_t now);
};
