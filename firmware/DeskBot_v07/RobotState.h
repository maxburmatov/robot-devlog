#pragma once

#include <stddef.h>

enum class RobotState {
  Curious,
  Waiting,
  Bored,
  Offended,
  Reconciling,
  Happy,
  Content,
  Drowsy,
  Sleep,
  Wake,
  Surprised,
  Startled,
  PickedUp,
  Dizzy,
  UpsideDown,
  Listening,
  Dancing,
  Count,
};

constexpr size_t ROBOT_STATE_COUNT =
    static_cast<size_t>(RobotState::Count);
