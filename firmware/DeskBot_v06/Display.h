#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include "RobotState.h"
#include "WeatherService.h"

class RobotDisplay {
 public:
  RobotDisplay();

  void begin();
  void showStartup();
  void showDistanceSensorError();
  // Renders already resolved local time; networking belongs to TimeService.
  void showClock(bool timeAvailable, uint8_t hour, uint8_t minute,
                 uint8_t second, uint8_t weekday, uint8_t day,
                 uint8_t month);
  void showWeather(const WeatherData& data, WeatherStatus status);
  // Starts a non-blocking horizontal slide between top-level screens.
  void startScreenTransition(bool contentMovesLeft, unsigned long now);
  // Advances an active transition. Returns true while it owns this frame.
  bool updateScreenTransition(unsigned long now);
  // Temporarily overlays the current runtime sound state on the face screen.
  void showSoundStatus(bool enabled, unsigned long now);
  // Draws the current frame and reports whether a new phrase became visible.
  bool update(RobotState state, unsigned long now,
              unsigned long stateStartedAt, uint32_t behaviorEpisodeId,
              int8_t gazeOffset);

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
  RobotState displayedState_ = RobotState::Curious;
  bool displayedStateInitialized_ = false;
  unsigned long soundStatusUntil_ = 0;
  bool soundStatusEnabled_ = false;
  static constexpr size_t FRAME_BUFFER_SIZE = 128 * 64 / 8;
  uint8_t transitionFrom_[FRAME_BUFFER_SIZE] = {};
  uint8_t transitionTo_[FRAME_BUFFER_SIZE] = {};
  unsigned long transitionStartedAt_ = 0;
  bool transitionMovesLeft_ = true;
  bool transitionActive_ = false;
  bool transitionTargetReady_ = false;

  void presentFrame(unsigned long now);
  void drawScreenTransition(unsigned long now);
  void drawHeart(int centerX, int centerY);
  void drawSoundStatus(bool enabled);
  void drawCenteredText(const char* text);
  void drawCurious(unsigned long now, int8_t gazeOffset,
                   uint8_t phraseVariant);
  void drawWaiting(unsigned long now, uint8_t phraseVariant,
                   bool phraseTransitionActive);
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
  void drawWeatherIcon(uint8_t weatherCode, bool isDay, int x, int y);
  void drawSun(int x, int y);
  void drawMoon(int x, int y);
  void drawCloud(int x, int y);
  void drawRain(int x, int y);
  void drawSnow(int x, int y);
  void drawThunderstorm(int x, int y);
  void drawFog(int x, int y);
};
