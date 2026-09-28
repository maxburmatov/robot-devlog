#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include "RobotState.h"

class RobotDisplay {
 public:
  RobotDisplay();

  void begin();
  void showStartup();
  void showDistanceSensorError();
  void update(RobotState state, unsigned long now, unsigned long stateStartedAt,
              int8_t gazeOffset);

 private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled_;

  void drawHeart(int centerX, int centerY);
  void drawCurious(unsigned long now, int8_t gazeOffset);
  void drawHappy(unsigned long now);
  void drawTilt(unsigned long now, int8_t direction);
  void drawDrowsy(unsigned long now, unsigned long stateStartedAt);
  void drawSleep(unsigned long now);
  void drawWake(unsigned long now, unsigned long wakeStartedAt);
  void drawGroggy(unsigned long now, unsigned long wakeStartedAt);
  void drawSurprised(unsigned long now);
  void drawPickedUp(unsigned long now);
  void drawDizzy(unsigned long now);
  void drawUpsideDown();
};
