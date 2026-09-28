#include "Display.h"

#include "Config.h"

RobotDisplay::RobotDisplay()
    : oled_(U8G2_R0, U8X8_PIN_NONE) {}

void RobotDisplay::begin() {
  oled_.setI2CAddress(config::OLED_ADDRESS * 2);
  oled_.begin();
  oled_.enableUTF8Print();
}

void RobotDisplay::showStartup() {
  oled_.clearBuffer();
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawStr(37, 25, "DESKBOT");
  oled_.drawUTF8(40, 45, "Запуск...");
  oled_.sendBuffer();
}

void RobotDisplay::showDistanceSensorError() {
  oled_.clearBuffer();
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8(31, 25, "Ошибка ToF");
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

void RobotDisplay::drawCurious(unsigned long now, int8_t gazeOffset) {
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
  oled_.drawUTF8(28, 63, "Наблюдаю...");
}

void RobotDisplay::drawHappy(unsigned long now) {
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
  oled_.drawUTF8(23, 63, "Ооо, ты здесь!");
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

void RobotDisplay::update(RobotState state, unsigned long now,
                          unsigned long stateStartedAt, int8_t gazeOffset) {
  oled_.clearBuffer();

  switch (state) {
    case RobotState::Curious:
      drawCurious(now, gazeOffset);
      break;
    case RobotState::Happy:
      drawHappy(now);
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
  }

  oled_.sendBuffer();
}
