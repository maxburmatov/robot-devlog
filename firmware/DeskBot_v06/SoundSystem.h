#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

#include "RobotState.h"

class SoundSystem {
 public:
  bool begin();

  // Starts the canonical sound for state. Sleep is intentionally silent.
  bool play(RobotState state);

  // Runtime mute keeps the I2S driver and audio task alive. Muting also drops
  // the pending request and interrupts the clip currently being played.
  void setEnabled(bool enabled);
  bool isEnabled() const;

 private:
  struct Request {
    RobotState state;
    uint8_t variant;
    uint32_t sequence;
  };

  static void audioTaskEntry(void* context);
  void audioTask();
  void playRequest(const Request& request);
  uint8_t chooseVariant(RobotState state);
  static const char* stateName(RobotState state);

  TaskHandle_t taskHandle_ = nullptr;
  QueueHandle_t requestQueue_ = nullptr;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  bool initialized_ = false;
  bool enabled_ = false;
  bool abortRequested_ = false;
  uint32_t nextSequence_ = 0;
  uint32_t currentSequence_ = 0;
  uint32_t pendingSequence_ = 0;
  uint8_t lastVariant_[14] = {};
};
