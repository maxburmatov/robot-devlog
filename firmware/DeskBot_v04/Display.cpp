#include "Display.h"

#include "Config.h"

namespace {

constexpr unsigned long LONG_ANIMATION_PHASE_MS = 5000;
constexpr unsigned long CONTENT_PHRASE_MIN_MS = 6000;
constexpr unsigned long CONTENT_PHRASE_MAX_MS = 11000;
constexpr unsigned long BORED_PHRASE_MIN_MS = 7000;
constexpr unsigned long BORED_PHRASE_MAX_MS = 12000;
constexpr unsigned long TIME_REMINDER_MIN_MS = 45000;
constexpr unsigned long TIME_REMINDER_MAX_MS = 75000;
constexpr unsigned long PHRASE_TRANSITION_MS = 500;
constexpr uint8_t LONG_PHRASE_VARIANTS = 8;

unsigned long roundedMinutes(unsigned long elapsed) {
  unsigned long minutes = elapsed / 60000UL;
  if (elapsed % 60000UL >= 30000UL) {
    ++minutes;
  }
  return minutes;
}

bool deadlineReached(unsigned long now, unsigned long deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

unsigned long randomDuration(unsigned long minimum, unsigned long maximum) {
  return static_cast<unsigned long>(
      random(static_cast<long>(minimum), static_cast<long>(maximum + 1)));
}

uint8_t randomDifferentVariant(uint8_t current) {
  uint8_t next = static_cast<uint8_t>(random(LONG_PHRASE_VARIANTS - 1));
  if (next >= current) {
    ++next;
  }
  return next;
}

int trianglePulse(unsigned long elapsed, int maximum) {
  const unsigned long phase = elapsed % LONG_ANIMATION_PHASE_MS;
  const unsigned long half = LONG_ANIMATION_PHASE_MS / 2;
  const unsigned long rising =
      phase <= half ? phase : LONG_ANIMATION_PHASE_MS - phase;
  return static_cast<int>(rising * static_cast<unsigned long>(maximum) / half);
}

}  // namespace

RobotDisplay::RobotDisplay()
    : oled_(U8G2_R0, U8X8_PIN_NONE) {}

void RobotDisplay::begin() {
  oled_.setI2CAddress(config::OLED_ADDRESS * 2);
  oled_.begin();
  oled_.enableUTF8Print();
}

void RobotDisplay::showStartup() {
  oled_.clearBuffer();
  oled_.setFont(u8g2_font_6x12_tf);
  oled_.drawStr(37, 25, "DESKBOT");
  oled_.drawStr(32, 45, "Starting...");
  oled_.sendBuffer();
}

void RobotDisplay::showDistanceSensorError() {
  oled_.clearBuffer();
  oled_.setFont(u8g2_font_6x12_tf);
  oled_.drawStr(23, 25, "TOF ERROR");
  oled_.sendBuffer();
}

void RobotDisplay::drawHeart(int centerX, int centerY) {
  oled_.drawDisc(centerX - 4, centerY - 2, 4);
  oled_.drawDisc(centerX + 4, centerY - 2, 4);

  for (int row = 0; row < 9; ++row) {
    const int width = 17 - row * 2;
    if (width > 0) {
      oled_.drawHLine(centerX - 8 + row, centerY + row, width);
    }
  }
}

void RobotDisplay::drawCenteredText(const char* text) {
  const int width = oled_.getUTF8Width(text);
  oled_.drawUTF8(width < 128 ? (128 - width) / 2 : 0, 63, text);
}

void RobotDisplay::drawCurious(unsigned long now,
                               int8_t gazeOffset,
                               uint8_t phraseVariant) {
  const int gazePositions[] = {-4, 0, 4, 0};
  const int gaze = gazeOffset != 0 ? gazeOffset : gazePositions[(now / 900) % 4];
  const bool blinking =
      (now % config::BLINK_INTERVAL_MS) < config::BLINK_DURATION_MS;

  if (blinking) {
    oled_.drawHLine(15, 26, 28);
    oled_.drawHLine(85, 26, 28);
  } else {
    oled_.drawRFrame(15, 13, 28, 26, 7);
    oled_.drawDisc(29 + gaze, 26, 5);
    oled_.drawRFrame(85, 13, 28, 26, 7);
    oled_.drawDisc(99 + gaze, 26, 5);
  }

  oled_.drawCircle(64, 46, 4);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  const char* phrase = nullptr;
  switch (phraseVariant) {
    case 0:
      phrase = "Наблюдаю...";
      break;
    case 1:
      phrase = "Что там?";
      break;
    case 2:
      phrase = "Хм, интересно";
      break;
    case 3:
      phrase = "Сканирую тишину";
      break;
    case 4:
      phrase = "Что-то изменилось";
      break;
    case 5:
      phrase = "Есть движение";
      break;
    case 6:
      phrase = "Кто здесь?";
      break;
    default:
      phrase = "Проверяю...";
      break;
  }
  drawCenteredText(phrase);
}

void RobotDisplay::drawHappy(unsigned long now, uint8_t phraseVariant) {
  const bool blinking =
      (now % config::BLINK_INTERVAL_MS) < config::BLINK_DURATION_MS;

  if (blinking) {
    oled_.drawHLine(15, 26, 28);
    oled_.drawHLine(85, 26, 28);
  } else {
    oled_.drawRFrame(15, 13, 28, 26, 7);
    oled_.drawRFrame(85, 13, 28, 26, 7);
    drawHeart(29, 23);
    drawHeart(99, 23);
  }

  oled_.drawLine(48, 43, 52, 47);
  oled_.drawLine(52, 47, 58, 50);
  oled_.drawLine(58, 50, 64, 51);
  oled_.drawLine(64, 51, 70, 50);
  oled_.drawLine(70, 50, 76, 47);
  oled_.drawLine(76, 47, 80, 43);

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  switch (phraseVariant) {
    case 0:
      oled_.drawUTF8(26, 63, "О! Ты здесь!");
      break;
    case 1:
      oled_.drawUTF8(36, 63, "Нашёлся!");
      break;
    default:
      oled_.drawUTF8(29, 63, "Ура, привет!");
      break;
  }
}

void RobotDisplay::drawContent(unsigned long now,
                               unsigned long stateStartedAt,
                               int8_t gazeOffset,
                               uint8_t phraseVariant,
                               bool showDuration,
                               bool phraseTransitionActive) {
  const unsigned long elapsed = now - stateStartedAt;
  const uint8_t animation = (elapsed / LONG_ANIMATION_PHASE_MS) % 3;
  const int pulse = trianglePulse(elapsed, 4);
  const int animatedGaze = animation == 0 ? pulse : (animation == 2 ? -pulse : 0);
  const int gaze = gazeOffset != 0 ? gazeOffset : animatedGaze;
  const int relaxedEyes = animation == 1 ? trianglePulse(elapsed, 3) : 0;
  const int smileDepth = animation == 1 ? trianglePulse(elapsed, 2) : 0;
  const bool blinking =
      (now % config::BLINK_INTERVAL_MS) < config::BLINK_DURATION_MS;

  if (blinking) {
    oled_.drawHLine(15, 26 + relaxedEyes, 28);
    oled_.drawHLine(85, 26 + relaxedEyes, 28);
  } else {
    oled_.drawRFrame(15, 13 + relaxedEyes, 28, 26 - relaxedEyes, 7);
    oled_.drawDisc(29 + gaze, 26 + relaxedEyes, 5);
    oled_.drawRFrame(85, 13 + relaxedEyes, 28, 26 - relaxedEyes, 7);
    oled_.drawDisc(99 + gaze, 26 + relaxedEyes, 5);
  }

  oled_.drawLine(53, 45, 58, 48);
  oled_.drawLine(58, 48, 64, 49 + smileDepth);
  oled_.drawLine(64, 49 + smileDepth, 70, 48);
  oled_.drawLine(70, 48, 75, 45);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  if (phraseTransitionActive) {
    drawCenteredText("...");
    return;
  }

  const unsigned long minutes = roundedMinutes(elapsed);
  if (showDuration && minutes > 0) {
    char message[48];
    snprintf(message, sizeof(message), "Ты здесь уже %luм", minutes);
    drawCenteredText(message);
    return;
  }

  const char* phrase = nullptr;
  switch (phraseVariant) {
    case 0:
      phrase = "Я рядом";
      break;
    case 1:
      phrase = "Мне хорошо";
      break;
    case 2:
      phrase = "Посидим?";
      break;
    case 3:
      phrase = "Режим: уют";
      break;
    case 4:
      phrase = "Мир на месте";
      break;
    case 5:
      phrase = "Хорошо молчим";
      break;
    case 6:
      phrase = "Никуда не спешим";
      break;
    default:
      phrase = "Тишина тоже ответ";
      break;
  }
  drawCenteredText(phrase);
}

void RobotDisplay::drawWaiting(unsigned long now, uint8_t phraseVariant) {
  const unsigned long phase = (now / 650) % 6;
  int gaze = 0;
  if (phase == 0 || phase == 1) {
    gaze = -6;
  } else if (phase == 3 || phase == 4) {
    gaze = 6;
  }
  const bool blinking =
      (now % (config::BLINK_INTERVAL_MS + 900)) < config::BLINK_DURATION_MS;
  if (blinking) {
    oled_.drawHLine(15, 26, 28);
    oled_.drawHLine(85, 26, 28);
  } else {
    oled_.drawRFrame(15, 13, 28, 26, 7);
    oled_.drawDisc(29 + gaze, 26, 5);
    oled_.drawRFrame(85, 13, 28, 26, 7);
    oled_.drawDisc(99 + gaze, 26, 5);
  }
  oled_.drawCircle(64, 47, 3);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  const char* phrase = nullptr;
  switch (phraseVariant) {
    case 0:
      phrase = "Куда ты?";
      break;
    case 1:
      phrase = "Я подожду";
      break;
    case 2:
      phrase = "Ты скоро?";
      break;
    case 3:
      phrase = "Место сохраню";
      break;
    case 4:
      phrase = "Я пока здесь";
      break;
    case 5:
      phrase = "Не торопись...";
      break;
    case 6:
      phrase = "Вернёшься?";
      break;
    default:
      phrase = "Жду продолжение";
      break;
  }
  drawCenteredText(phrase);
}

void RobotDisplay::drawBored(unsigned long now,
                             unsigned long stateStartedAt,
                             uint8_t phraseVariant,
                             bool showDuration,
                             bool phraseTransitionActive) {
  const unsigned long elapsed = now - stateStartedAt;
  const uint8_t animation = (elapsed / LONG_ANIMATION_PHASE_MS) % 3;
  const int pulse = trianglePulse(elapsed, 5);
  const int gaze = animation == 0 ? -pulse : (animation == 2 ? pulse / 2 : 0);
  const int droop = animation == 1 ? trianglePulse(elapsed, 3) : 0;
  const int pupilDrop = droop / 2;
  const int sigh = animation == 2 ? trianglePulse(elapsed, 3) : 0;
  const bool blinking =
      (now % (config::BLINK_INTERVAL_MS + 1700)) < config::BLINK_DURATION_MS;
  if (blinking) {
    oled_.drawHLine(17, 29 + droop, 24);
    oled_.drawHLine(87, 29 + droop, 24);
  } else {
    oled_.drawRFrame(15, 19 + droop, 28, 18 - droop, 6);
    oled_.drawDisc(29 + gaze, 31 + pupilDrop, 4);
    oled_.drawRFrame(85, 19 + droop, 28, 18 - droop, 6);
    oled_.drawDisc(99 + gaze, 31 + pupilDrop, 4);
    oled_.drawHLine(17, 23 + droop, 24);
    oled_.drawHLine(87, 23 + droop, 24);
  }
  oled_.drawLine(56, 49, 61, 46);
  oled_.drawLine(61, 46, 64, 46 + sigh);
  oled_.drawLine(64, 46 + sigh, 67, 46);
  oled_.drawLine(67, 46, 72, 49);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  if (phraseTransitionActive) {
    drawCenteredText("...");
    return;
  }

  const unsigned long minutes = roundedMinutes(elapsed);
  if (showDuration && minutes > 0) {
    char message[48];
    snprintf(message, sizeof(message), "Никого нет уже %luм", minutes);
    drawCenteredText(message);
    return;
  }

  const char* phrase = nullptr;
  switch (phraseVariant) {
    case 0:
      phrase = "Скучно...";
      break;
    case 1:
      phrase = "Опять один...";
      break;
    case 2:
      phrase = "Пиксели пересчитаны";
      break;
    case 3:
      phrase = "Тишина победила";
      break;
    case 4:
      phrase = "Даже пыль занята";
      break;
    case 5:
      phrase = "Поговорил со стеной";
      break;
    case 6:
      phrase = "Эхо тоже ушло";
      break;
    default:
      phrase = "Жду поворот сюжета";
      break;
  }
  drawCenteredText(phrase);
}

void RobotDisplay::drawOffended(unsigned long now, uint8_t phraseVariant) {
  const int gaze = ((now / 500) % 2 == 0) ? -6 : -5;
  oled_.drawLine(16, 13, 41, 18);
  oled_.drawLine(87, 18, 112, 13);
  oled_.drawRFrame(15, 17, 28, 20, 6);
  oled_.drawDisc(29 + gaze, 29, 4);
  oled_.drawRFrame(85, 17, 28, 20, 6);
  oled_.drawDisc(99 + gaze, 29, 4);
  oled_.drawHLine(56, 48, 16);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  switch (phraseVariant) {
    case 0:
      oled_.drawUTF8(29, 63, "А, это ты.");
      break;
    case 1:
      oled_.drawUTF8(30, 63, "Я не скучал");
      break;
    default:
      oled_.drawUTF8(28, 63, "Поздновато...");
      break;
  }
}

void RobotDisplay::drawReconciling(unsigned long now,
                                   unsigned long stateStartedAt,
                                   uint8_t phraseVariant) {
  const unsigned long elapsed = now - stateStartedAt;
  const unsigned long capped =
      elapsed < config::BOREDOM_RECONCILE_MS
          ? elapsed
          : config::BOREDOM_RECONCILE_MS;
  const int gaze = -6 + static_cast<int>(6UL * capped /
                                         config::BOREDOM_RECONCILE_MS);
  const int eyeY = 17 - static_cast<int>(4UL * capped /
                                         config::BOREDOM_RECONCILE_MS);
  const int eyeHeight = 20 + static_cast<int>(6UL * capped /
                                              config::BOREDOM_RECONCILE_MS);
  oled_.drawRFrame(15, eyeY, 28, eyeHeight, 7);
  oled_.drawDisc(29 + gaze, 27, 5);
  oled_.drawRFrame(85, eyeY, 28, eyeHeight, 7);
  oled_.drawDisc(99 + gaze, 27, 5);
  oled_.drawLine(55, 48, 61, 46);
  oled_.drawLine(61, 46, 67, 46);
  oled_.drawLine(67, 46, 73, 48);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  switch (phraseVariant) {
    case 0:
      oled_.drawUTF8(29, 63, "Ладно уж...");
      break;
    case 1:
      oled_.drawUTF8(49, 63, "Мир?");
      break;
    default:
      oled_.drawUTF8(34, 63, "Ну хорошо");
      break;
  }
}

void RobotDisplay::drawDrowsy(unsigned long now,
                              unsigned long stateStartedAt) {
  const unsigned long elapsed = now - stateStartedAt;
  const int sway = ((elapsed / 450) % 2 == 0) ? -1 : 1;

  // Веки постепенно закрываются на протяжении всей предсонной анимации.
  if (elapsed < 700) {
    oled_.drawRFrame(15, 15, 28, 23, 7);
    oled_.drawDisc(29 + sway, 28, 4);
    oled_.drawRFrame(85, 15, 28, 23, 7);
    oled_.drawDisc(99 + sway, 28, 4);

    oled_.setDrawColor(0);
    oled_.drawBox(14, 14, 30, 8);
    oled_.drawBox(84, 14, 30, 8);
    oled_.setDrawColor(1);
    oled_.drawHLine(17, 22, 24);
    oled_.drawHLine(87, 22, 24);
  } else if (elapsed < 1800) {
    oled_.drawRFrame(15, 19, 28, 18, 7);
    oled_.drawDisc(29 + sway, 30, 3);
    oled_.drawRFrame(85, 19, 28, 18, 7);
    oled_.drawDisc(99 + sway, 30, 3);

    oled_.setDrawColor(0);
    oled_.drawBox(14, 18, 30, 9);
    oled_.drawBox(84, 18, 30, 9);
    oled_.setDrawColor(1);
    oled_.drawHLine(17, 27, 24);
    oled_.drawHLine(87, 27, 24);
  } else {
    oled_.drawHLine(17, 29, 24);
    oled_.drawHLine(87, 29, 24);
  }

  // Рот раскрывается для зевка и снова уменьшается перед сном.
  int mouthRadius = 3;
  if (elapsed >= 500 && elapsed < 1900) {
    mouthRadius = 6;
  } else if (elapsed >= 1900) {
    mouthRadius = 4;
  }
  oled_.drawCircle(64, 47, mouthRadius);
  if (mouthRadius >= 6) {
    oled_.drawCircle(64, 47, mouthRadius - 1);
  }

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8(34, 63, "Я спать...");
}

void RobotDisplay::drawSleep(unsigned long now) {
  oled_.drawHLine(15, 26, 28);
  oled_.drawHLine(85, 26, 28);
  oled_.drawHLine(59, 46, 10);

  oled_.setFont(u8g2_font_6x13_tf);
  const unsigned long phase = (now / 600) % 3;
  oled_.drawStr(99, 20, "Z");
  if (phase >= 1) {
    oled_.drawStr(108, 13, "Z");
  }
  if (phase >= 2) {
    oled_.drawStr(117, 7, "Z");
  }

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8(42, 63, "Сплю...");
}

void RobotDisplay::drawWake(unsigned long now, unsigned long wakeStartedAt) {
  const unsigned long elapsed = now - wakeStartedAt;
  oled_.setFont(u8g2_font_6x13_t_cyrillic);

  if (elapsed < 300) {
    oled_.drawHLine(15, 26, 28);
    oled_.drawHLine(85, 26, 28);
    oled_.drawUTF8(43, 63, "Ммм...");
  } else if (elapsed < 750) {
    oled_.drawRFrame(15, 13, 28, 26, 7);
    oled_.drawDisc(29, 26, 5);
    oled_.drawHLine(85, 26, 28);
    oled_.drawUTF8(28, 63, "Просыпаюсь...");
  } else {
    oled_.drawRFrame(15, 13, 28, 26, 7);
    oled_.drawDisc(29, 26, 5);
    oled_.drawRFrame(85, 13, 28, 26, 7);
    oled_.drawDisc(99, 26, 5);
    oled_.drawCircle(64, 46, 4);
    oled_.drawUTF8(43, 63, "Ага...");
  }
}

void RobotDisplay::drawSurprised(unsigned long now) {
  const unsigned long phase = (now / 130) % 4;
  const int lift = (phase == 1 || phase == 2) ? 2 : 0;
  const int pupilRadius = (phase == 2) ? 6 : 5;
  const int mouthRadius = (phase == 0 || phase == 3) ? 4 : 6;

  // Брови подпрыгивают, а глаза на мгновение расширяются.
  oled_.drawLine(17, 9 - lift, 40, 6 - lift);
  oled_.drawLine(88, 6 - lift, 111, 9 - lift);
  oled_.drawRFrame(14, 12 - lift, 30, 28 + lift, 9);
  oled_.drawDisc(29, 26, pupilRadius);
  oled_.drawRFrame(84, 12 - lift, 30, 28 + lift, 9);
  oled_.drawDisc(99, 26, pupilRadius);

  // Пульсирующий круглый рот завершает удивлённое выражение.
  oled_.drawCircle(64, 47, mouthRadius);
  if (mouthRadius > 4) {
    oled_.drawCircle(64, 47, mouthRadius - 1);
  }

  // Короткие лучи появляются в самой выразительной фазе.
  if (phase == 2) {
    oled_.drawLine(6, 22, 11, 22);
    oled_.drawLine(117, 22, 122, 22);
    oled_.drawLine(9, 12, 13, 15);
    oled_.drawLine(115, 15, 119, 12);
  }

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8(34, 63, "Ох, блииин!");
}

void RobotDisplay::drawPickedUp(unsigned long now) {
  const int wobble = ((now / 180) % 2 == 0) ? -1 : 1;

  // Приподнятые внутренние края бровей и взгляд вниз создают тревожную,
  // но не агрессивную просьбу вернуть робота на устойчивую поверхность.
  oled_.drawLine(17, 8, 40, 12);
  oled_.drawLine(88, 12, 111, 8);
  oled_.drawRFrame(15 + wobble, 13, 28, 27, 8);
  oled_.drawDisc(29 + wobble, 30, 5);
  oled_.drawRFrame(85 + wobble, 13, 28, 27, 8);
  oled_.drawDisc(99 + wobble, 30, 5);

  oled_.drawLine(56, 50, 60, 46);
  oled_.drawLine(60, 46, 64, 45);
  oled_.drawLine(64, 45, 68, 46);
  oled_.drawLine(68, 46, 72, 50);

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8(25, 63, "Поставь меня!");
}

void RobotDisplay::drawDizzy(unsigned long now) {
  const int wobble = ((now / 120) % 2 == 0) ? -2 : 2;

  oled_.drawLine(18 + wobble, 17, 40 + wobble, 35);
  oled_.drawLine(40 + wobble, 17, 18 + wobble, 35);
  oled_.drawLine(88 - wobble, 17, 110 - wobble, 35);
  oled_.drawLine(110 - wobble, 17, 88 - wobble, 35);
  oled_.drawLine(53, 48, 59, 44);
  oled_.drawLine(59, 44, 65, 48);
  oled_.drawLine(65, 48, 71, 44);
  oled_.drawLine(71, 44, 77, 48);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8(34, 63, "Кружится...");
}

void RobotDisplay::drawUpsideDown() {
  oled_.drawRFrame(85, 25, 28, 26, 7);
  oled_.drawDisc(99, 38, 5);
  oled_.drawRFrame(15, 25, 28, 26, 7);
  oled_.drawDisc(29, 38, 5);
  oled_.drawCircle(64, 14, 5);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8(31, 63, "Перевернули!");
}

void RobotDisplay::update(RobotState state, unsigned long now,
                          unsigned long stateStartedAt,
                          uint32_t behaviorEpisodeId, int8_t gazeOffset) {
  const bool usesRandomPhrase =
      state == RobotState::Happy || state == RobotState::Offended ||
      state == RobotState::Reconciling;
  if (usesRandomPhrase &&
      (!fastPhraseInitialized_ || fastPhraseState_ != state ||
       fastPhraseEpisodeId_ != behaviorEpisodeId)) {
    fastPhraseState_ = state;
    fastPhraseEpisodeId_ = behaviorEpisodeId;
    fastPhraseVariant_ = static_cast<uint8_t>(random(3));
    fastPhraseInitialized_ = true;
  }

  const bool usesLongPhrase =
      state == RobotState::Curious || state == RobotState::Waiting ||
      state == RobotState::Bored || state == RobotState::Content;
  if (usesLongPhrase) {
    const bool sameStateEpisode =
        longPhraseInitialized_ && longPhraseState_ == state &&
        longPhraseEpisodeId_ == behaviorEpisodeId;
    const bool variableDurationState =
        state == RobotState::Content || state == RobotState::Bored;

    if (!sameStateEpisode) {
      longPhraseState_ = state;
      longPhraseEpisodeId_ = behaviorEpisodeId;
      longPhraseVariant_ = static_cast<uint8_t>(random(LONG_PHRASE_VARIANTS));
      longPhraseShowsDuration_ = false;
      longPhraseTransitionActive_ = false;
      if (variableDurationState) {
        const unsigned long phraseDuration =
            state == RobotState::Content
                ? randomDuration(CONTENT_PHRASE_MIN_MS,
                                 CONTENT_PHRASE_MAX_MS)
                : randomDuration(BORED_PHRASE_MIN_MS, BORED_PHRASE_MAX_MS);
        longPhraseChangesAt_ = now + phraseDuration;
        longPhraseTimeReminderAt_ =
            now + randomDuration(TIME_REMINDER_MIN_MS, TIME_REMINDER_MAX_MS);
      }
      longPhraseInitialized_ = true;
    } else if (variableDurationState) {
      if (longPhraseTransitionActive_ &&
          deadlineReached(now, longPhraseTransitionUntil_)) {
        longPhraseTransitionActive_ = false;
      }

      if (!longPhraseTransitionActive_ &&
          deadlineReached(now, longPhraseChangesAt_)) {
        const unsigned long elapsed = now - stateStartedAt;
        const bool showDuration =
            deadlineReached(now, longPhraseTimeReminderAt_) &&
            roundedMinutes(elapsed) > 0;
        longPhraseShowsDuration_ = showDuration;
        if (showDuration) {
          longPhraseTimeReminderAt_ =
              now + randomDuration(TIME_REMINDER_MIN_MS,
                                   TIME_REMINDER_MAX_MS);
        } else {
          longPhraseVariant_ = randomDifferentVariant(longPhraseVariant_);
        }

        longPhraseTransitionActive_ = true;
        longPhraseTransitionUntil_ = now + PHRASE_TRANSITION_MS;

        const unsigned long phraseDuration =
            state == RobotState::Content
                ? randomDuration(CONTENT_PHRASE_MIN_MS,
                                 CONTENT_PHRASE_MAX_MS)
                : randomDuration(BORED_PHRASE_MIN_MS,
                                 BORED_PHRASE_MAX_MS);
        longPhraseChangesAt_ = longPhraseTransitionUntil_ + phraseDuration;
      }
    }
  }

  oled_.clearBuffer();

  switch (state) {
    case RobotState::Curious:
      drawCurious(now, gazeOffset, longPhraseVariant_);
      break;
    case RobotState::Waiting:
      drawWaiting(now, longPhraseVariant_);
      break;
    case RobotState::Bored:
      drawBored(now, stateStartedAt, longPhraseVariant_,
                longPhraseShowsDuration_, longPhraseTransitionActive_);
      break;
    case RobotState::Offended:
      drawOffended(now, fastPhraseVariant_);
      break;
    case RobotState::Reconciling:
      drawReconciling(now, stateStartedAt, fastPhraseVariant_);
      break;
    case RobotState::Happy:
      drawHappy(now, fastPhraseVariant_);
      break;
    case RobotState::Content:
      drawContent(now, stateStartedAt, gazeOffset, longPhraseVariant_,
                  longPhraseShowsDuration_, longPhraseTransitionActive_);
      break;
    case RobotState::Drowsy:
      drawDrowsy(now, stateStartedAt);
      break;
    case RobotState::Sleep:
      drawSleep(now);
      break;
    case RobotState::Wake:
      drawWake(now, stateStartedAt);
      break;
    case RobotState::Surprised:
      drawSurprised(now);
      break;
    case RobotState::PickedUp:
      drawPickedUp(now);
      break;
    case RobotState::Dizzy:
      drawDizzy(now);
      break;
    case RobotState::UpsideDown:
      drawUpsideDown();
      break;
  }

  oled_.sendBuffer();
}
