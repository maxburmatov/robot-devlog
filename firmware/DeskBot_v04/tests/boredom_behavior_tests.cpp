#include <assert.h>
#include <stdint.h>

#include "../BoredomBehavior.h"

namespace {

const BoredomConfig kConfig = {
    600, 700, 3, 1200, 4500, 12000, 2500, 1200, 2200,
};

void confirmNear(BoredomBehavior& behavior, uint32_t startedAt) {
  behavior.updateMeasurement(true, 500, startedAt);
  behavior.updateMeasurement(true, 500, startedAt + 100);
  behavior.updateMeasurement(true, 500, startedAt + 200);
}

void confirmFar(BoredomBehavior& behavior, uint32_t startedAt) {
  behavior.updateMeasurement(true, 800, startedAt);
  behavior.updateMeasurement(true, 800, startedAt + 100);
  behavior.updateMeasurement(true, 800, startedAt + 200);
}

void testFirstAppearanceAndStablePresence() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 100);
  assert(behavior.state() == RobotState::Happy);
  behavior.update(2499);
  assert(behavior.state() == RobotState::Happy);
  behavior.update(2500);
  assert(behavior.state() == RobotState::Content);
  confirmNear(behavior, 2700);
  assert(behavior.state() == RobotState::Content);
}

void testWaitingBoredAndOffendedReturn() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 0);
  behavior.update(2400);
  confirmFar(behavior, 2500);
  assert(behavior.state() == RobotState::Curious);
  behavior.update(7199);
  assert(behavior.state() == RobotState::Curious);
  behavior.update(7200);
  assert(behavior.state() == RobotState::Waiting);
  behavior.update(14700);
  assert(behavior.state() == RobotState::Bored);

  confirmNear(behavior, 14800);
  assert(behavior.state() == RobotState::Offended);
  behavior.update(17500);
  assert(behavior.state() == RobotState::Reconciling);
  behavior.update(18700);
  assert(behavior.state() == RobotState::Content);
}

void testShortAbsenceReturnsDirectlyToHappy() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 0);
  behavior.update(2400);
  confirmFar(behavior, 2500);
  confirmNear(behavior, 4000);
  assert(behavior.state() == RobotState::Happy);
}

void testHysteresisAndInvalidMeasurements() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 0);
  behavior.update(2400);

  behavior.updateMeasurement(true, 800, 2500);
  behavior.updateMeasurement(false, 0, 2600);
  behavior.updateMeasurement(true, 800, 2700);
  behavior.updateMeasurement(true, 650, 2800);
  behavior.updateMeasurement(true, 800, 2900);
  assert(behavior.state() == RobotState::Content);

  behavior.updateMeasurement(false, 0, 3000);
  behavior.updateMeasurement(false, 0, 4200);
  assert(behavior.state() == RobotState::Content);
  confirmNear(behavior, 4300);
  assert(behavior.state() == RobotState::Content);

  BoredomBehavior unavailableAtStartup(kConfig);
  unavailableAtStartup.reset(0);
  unavailableAtStartup.updateMeasurement(false, 0, 0);
  unavailableAtStartup.updateMeasurement(false, 0, 1200);
  assert(unavailableAtStartup.state() == RobotState::Curious);
  confirmNear(unavailableAtStartup, 1300);
  assert(unavailableAtStartup.state() == RobotState::Happy);
}

void testDepartureCancelsReconciliation() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 0);
  behavior.update(2400);
  confirmFar(behavior, 2500);
  behavior.update(14700);
  behavior.update(14701);
  confirmNear(behavior, 14800);
  assert(behavior.state() == RobotState::Offended);
  confirmFar(behavior, 15300);
  assert(behavior.state() == RobotState::Curious);
}

void testSustainedSignalFailMeansDepartureAfterPresence() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 0);
  behavior.update(2400);

  behavior.updateMeasurement(false, 0, 2500);
  behavior.updateMeasurement(false, 0, 3000);
  behavior.updateMeasurement(false, 0, 3699);
  assert(behavior.state() == RobotState::Content);

  behavior.updateMeasurement(false, 0, 3700);
  assert(behavior.state() == RobotState::Curious);
  behavior.update(8200);
  assert(behavior.state() == RobotState::Waiting);
  behavior.update(15700);
  assert(behavior.state() == RobotState::Bored);
}

void testLateReturnDoesNotDependOnIntermediateTick() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 0);
  behavior.update(2400);
  confirmFar(behavior, 2500);
  confirmNear(behavior, 14700);
  assert(behavior.state() == RobotState::Offended);
}

void testStateRevisionIgnoresMeasurementTimerCompensation() {
  BoredomBehavior behavior(kConfig);
  behavior.reset(0);
  confirmNear(behavior, 0);
  behavior.update(2400);
  confirmFar(behavior, 2500);
  behavior.update(7200);
  behavior.update(14700);
  assert(behavior.state() == RobotState::Bored);

  const uint32_t revision = behavior.stateRevision();
  const uint32_t enteredAt = behavior.stateEnteredAt();
  const uint32_t timerStartedAt = behavior.stateStartedAt();

  behavior.updateMeasurement(false, 0, 14800);
  behavior.updateMeasurement(true, 800, 14900);

  assert(behavior.state() == RobotState::Bored);
  assert(behavior.stateRevision() == revision);
  assert(behavior.stateEnteredAt() == enteredAt);
  assert(behavior.stateStartedAt() != timerStartedAt);
}

void testResetAndMillisWrap() {
  BoredomBehavior behavior(kConfig);
  const uint32_t start = 0xFFFFFF00UL;
  behavior.reset(start);
  confirmNear(behavior, start + 10U);
  behavior.update(start + 2410U);
  assert(behavior.state() == RobotState::Content);
  confirmFar(behavior, start + 2500U);
  behavior.update(start + 7200U);
  assert(behavior.state() == RobotState::Waiting);
  behavior.update(start + 14700U);
  assert(behavior.state() == RobotState::Bored);
  behavior.reset(start + 14800U);
  assert(behavior.state() == RobotState::Curious);
}

}  // namespace

int main() {
  testFirstAppearanceAndStablePresence();
  testWaitingBoredAndOffendedReturn();
  testShortAbsenceReturnsDirectlyToHappy();
  testHysteresisAndInvalidMeasurements();
  testDepartureCancelsReconciliation();
  testSustainedSignalFailMeansDepartureAfterPresence();
  testLateReturnDoesNotDependOnIntermediateTick();
  testStateRevisionIgnoresMeasurementTimerCompensation();
  testResetAndMillisWrap();
  return 0;
}
