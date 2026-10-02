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
              uint32_t behaviorEpisodeId, int8_t gazeOffset);

 private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled_;
  RobotState fastPhraseState_ = RobotState::Happy;
  uint32_t fastPhraseEpisodeId_ = 0;
  uint8_t fastPhraseVariant_ = 0;
  bool fastPhraseInitialized_ = false;
  RobotState longPhraseState_ = RobotState::Curious;
  uint32_t longPhraseEpisodeId_ = 0;
  unsigned long longPhraseChangesAt_ = 0;
  unsigned long longPhraseTimeReminderAt_ = 0;
  unsigned long longPhraseTransitionUntil_ = 0;
  uint8_t longPhraseVariant_ = 0;
  bool longPhraseShowsDuration_ = false;
  bool longPhraseTransitionActive_ = false;
  bool longPhraseInitialized_ = false;

  void drawHeart(int centerX, int centerY);
  void drawCenteredText(const char* text);
  void drawCurious(unsigned long now, int8_t gazeOffset,
                   uint8_t phraseVariant);
  void drawWaiting(unsigned long now, uint8_t phraseVariant);
  void drawBored(unsigned long now, unsigned long stateStartedAt,
                 uint8_t phraseVariant, bool showDuration,
                 bool phraseTransitionActive);
  void drawOffended(unsigned long now, uint8_t phraseVariant);
  void drawReconciling(unsigned long now, unsigned long stateStartedAt,
                       uint8_t phraseVariant);
  void drawHappy(unsigned long now, uint8_t phraseVariant);
  void drawContent(unsigned long now, unsigned long stateStartedAt,
                   int8_t gazeOffset, uint8_t phraseVariant,
                   bool showDuration, bool phraseTransitionActive);
  void drawDrowsy(unsigned long now, unsigned long stateStartedAt);
  void drawSleep(unsigned long now);
  void drawWake(unsigned long now, unsigned long wakeStartedAt);
  void drawSurprised(unsigned long now);
  void drawPickedUp(unsigned long now);
  void drawDizzy(unsigned long now);
  void drawUpsideDown();
};
