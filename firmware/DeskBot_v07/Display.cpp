#include "Display.h"

#include <cmath>
#include <cstring>

#include "Config.h"
#include "Phrases.h"

namespace {

constexpr unsigned long LONG_ANIMATION_PHASE_MS = 5000;
constexpr unsigned long CONTENT_PHRASE_MIN_MS = 6000;
constexpr unsigned long CONTENT_PHRASE_MAX_MS = 11000;
constexpr unsigned long WAITING_PHRASE_MIN_MS = 2500;
constexpr unsigned long WAITING_PHRASE_MAX_MS = 3500;
constexpr unsigned long BORED_PHRASE_MIN_MS = 7000;
constexpr unsigned long BORED_PHRASE_MAX_MS = 12000;
constexpr unsigned long TIME_REMINDER_MIN_MS = 45000;
constexpr unsigned long TIME_REMINDER_MAX_MS = 75000;
constexpr unsigned long PHRASE_TRANSITION_MS = 500;
constexpr unsigned long SOUND_STATUS_DURATION_MS = 2000;
constexpr unsigned long SCREEN_TRANSITION_MS = 300;
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_PAGES = 8;

const char* const RUSSIAN_MONTHS[] = {
    "",          "ЯНВАРЯ",   "ФЕВРАЛЯ", "МАРТА",    "АПРЕЛЯ",
    "МАЯ",       "ИЮНЯ",     "ИЮЛЯ",    "АВГУСТА",  "СЕНТЯБРЯ",
    "ОКТЯБРЯ",   "НОЯБРЯ",   "ДЕКАБРЯ",
};

// tm_wday uses Sunday = 0 through Saturday = 6.
const char* const RUSSIAN_WEEKDAYS[] = {
    "ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ",
};

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

uint8_t phraseCountFor(RobotState state) {
  switch (state) {
    case RobotState::Curious:
      return phrases::count(phrases::CURIOUS);
    case RobotState::Waiting:
      return phrases::count(phrases::WAITING);
    case RobotState::Bored:
      return phrases::count(phrases::BORED);
    case RobotState::Offended:
      return phrases::count(phrases::OFFENDED);
    case RobotState::Reconciling:
      return phrases::count(phrases::RECONCILING);
    case RobotState::Happy:
      return phrases::count(phrases::HAPPY);
    case RobotState::Content:
      return phrases::count(phrases::CONTENT);
    case RobotState::Listening:
      return phrases::count(phrases::LISTENING);
    case RobotState::Startled:
      return phrases::count(phrases::STARTLED);
    default:
      return 1;
  }
}

unsigned long nextPhraseDuration(RobotState state) {
  switch (state) {
    case RobotState::Waiting:
      return randomDuration(WAITING_PHRASE_MIN_MS, WAITING_PHRASE_MAX_MS);
    case RobotState::Content:
      return randomDuration(CONTENT_PHRASE_MIN_MS, CONTENT_PHRASE_MAX_MS);
    case RobotState::Bored:
      return randomDuration(BORED_PHRASE_MIN_MS, BORED_PHRASE_MAX_MS);
    default:
      return 0;
  }
}

uint8_t randomDifferentVariant(uint8_t current, uint8_t variantCount) {
  if (variantCount <= 1) {
    return 0;
  }
  uint8_t next = static_cast<uint8_t>(random(variantCount - 1));
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

constexpr float MUSIC_PI = 3.14159265358979323846F;

struct PointF {
  float x;
  float y;
};

PointF rotatePoint(float localX, float localY, float centerX, float centerY,
                   float sine, float cosine) {
  return {centerX + localX * cosine - localY * sine,
          centerY + localX * sine + localY * cosine};
}

void drawRotatedPolyline(U8G2& oled, const PointF* points, size_t count,
                         float centerX, float centerY, float sine,
                         float cosine, bool close) {
  if (count < 2) return;
  PointF previous = rotatePoint(points[0].x, points[0].y, centerX, centerY,
                                sine, cosine);
  for (size_t i = 1; i < count; ++i) {
    const PointF current = rotatePoint(points[i].x, points[i].y, centerX,
                                       centerY, sine, cosine);
    oled.drawLine(lroundf(previous.x), lroundf(previous.y),
                  lroundf(current.x), lroundf(current.y));
    previous = current;
  }
  if (close) {
    const PointF first = rotatePoint(points[0].x, points[0].y, centerX,
                                     centerY, sine, cosine);
    oled.drawLine(lroundf(previous.x), lroundf(previous.y),
                  lroundf(first.x), lroundf(first.y));
  }
}

void drawRotatedEyeFrame(U8G2& oled, float centerX, float centerY,
                         float halfHeight, float sine, float cosine) {
  const float shoulder = halfHeight * 0.72F;
  const PointF outline[] = {
      {-8.0F, -halfHeight}, {8.0F, -halfHeight},
      {12.0F, -shoulder},   {14.0F, -halfHeight * 0.35F},
      {14.0F, halfHeight * 0.35F}, {12.0F, shoulder},
      {8.0F, halfHeight},   {-8.0F, halfHeight},
      {-12.0F, shoulder},   {-14.0F, halfHeight * 0.35F},
      {-14.0F, -halfHeight * 0.35F}, {-12.0F, -shoulder},
  };
  drawRotatedPolyline(oled, outline, sizeof(outline) / sizeof(outline[0]),
                      centerX, centerY, sine, cosine, true);
}

void drawRotatedHappyArc(U8G2& oled, float centerX, float centerY,
                         float sine, float cosine) {
  const PointF upper[] = {
      {-13.0F, 2.0F}, {-7.0F, -2.0F}, {0.0F, -4.0F},
      {7.0F, -2.0F},  {13.0F, 2.0F},
  };
  const PointF lower[] = {
      {-12.0F, 3.5F}, {-6.0F, 0.0F}, {0.0F, -1.5F},
      {6.0F, 0.0F},   {12.0F, 3.5F},
  };
  drawRotatedPolyline(oled, upper, sizeof(upper) / sizeof(upper[0]), centerX,
                      centerY, sine, cosine, false);
  drawRotatedPolyline(oled, lower, sizeof(lower) / sizeof(lower[0]), centerX,
                      centerY, sine, cosine, false);
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

void RobotDisplay::showSoundStatus(bool enabled, unsigned long now) {
  soundStatusEnabled_ = enabled;
  soundStatusUntil_ = now + SOUND_STATUS_DURATION_MS;
}

void RobotDisplay::startScreenTransition(bool contentMovesLeft,
                                         unsigned long now) {
  memcpy(transitionFrom_, oled_.getBufferPtr(), FRAME_BUFFER_SIZE);
  transitionStartedAt_ = now;
  transitionMovesLeft_ = contentMovesLeft;
  transitionActive_ = true;
  transitionTargetReady_ = false;
}

bool RobotDisplay::updateScreenTransition(unsigned long now) {
  if (!transitionActive_ || !transitionTargetReady_) {
    return false;
  }

  drawScreenTransition(now);
  return true;
}

void RobotDisplay::presentFrame(unsigned long now) {
  if (!transitionActive_) {
    oled_.sendBuffer();
    return;
  }

  memcpy(transitionTo_, oled_.getBufferPtr(), FRAME_BUFFER_SIZE);
  transitionTargetReady_ = true;
  drawScreenTransition(now);
}

void RobotDisplay::drawScreenTransition(unsigned long now) {
  const unsigned long elapsed = now - transitionStartedAt_;
  if (elapsed >= SCREEN_TRANSITION_MS) {
    memcpy(oled_.getBufferPtr(), transitionTo_, FRAME_BUFFER_SIZE);
    oled_.sendBuffer();
    transitionActive_ = false;
    transitionTargetReady_ = false;
    return;
  }

  // Cubic ease-out: a quick start with a soft landing on a tiny OLED.
  const float remaining =
      1.0F - static_cast<float>(elapsed) / SCREEN_TRANSITION_MS;
  const float eased = 1.0F - remaining * remaining * remaining;
  const int offset = static_cast<int>(eased * SCREEN_WIDTH + 0.5F);
  uint8_t* frame = oled_.getBufferPtr();

  memset(frame, 0, FRAME_BUFFER_SIZE);
  for (int page = 0; page < SCREEN_PAGES; ++page) {
    const int row = page * SCREEN_WIDTH;
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
      const int fromX = transitionMovesLeft_ ? x + offset : x - offset;
      const int toX = transitionMovesLeft_ ? x - (SCREEN_WIDTH - offset)
                                           : x + (SCREEN_WIDTH - offset);
      if (fromX >= 0 && fromX < SCREEN_WIDTH) {
        frame[row + x] = transitionFrom_[row + fromX];
      } else if (toX >= 0 && toX < SCREEN_WIDTH) {
        frame[row + x] = transitionTo_[row + toX];
      }
    }
  }
  oled_.sendBuffer();
}

void RobotDisplay::showClock(bool timeAvailable, uint8_t hour,
                             uint8_t minute, uint8_t second,
                             uint8_t weekday, uint8_t day, uint8_t month) {
  char timeText[6] = "--:--";
  char hourText[3] = "";
  char minuteText[3] = "";
  char dateText[32] = "НЕТ ВРЕМЕНИ";
  if (timeAvailable && weekday <= 6 && month >= 1 && month <= 12) {
    snprintf(timeText, sizeof(timeText), "%02u:%02u", hour, minute);
    snprintf(hourText, sizeof(hourText), "%02u", hour);
    snprintf(minuteText, sizeof(minuteText), "%02u", minute);
    snprintf(dateText, sizeof(dateText), "%s, %u %s",
             RUSSIAN_WEEKDAYS[weekday], day, RUSSIAN_MONTHS[month]);
  }

  oled_.clearBuffer();
  oled_.setFont(u8g2_font_logisoso32_tf);
  const int width = oled_.getStrWidth(timeText);
  const int timeX = width < 128 ? (128 - width) / 2 : 0;
  if (timeAvailable && weekday <= 6 && month >= 1 && month <= 12) {
    // Keep all digit positions fixed while only the colon changes visibility.
    // A space cannot be used here because this proportional font gives it a
    // different width, which makes the centered time jump every second.
    oled_.drawStr(timeX, 38, hourText);
    const int colonX = timeX + oled_.getStrWidth(hourText);
    if (second % 2 == 0) {
      oled_.drawStr(colonX, 38, ":");
    }
    const int minuteX = colonX + oled_.getStrWidth(":");
    oled_.drawStr(minuteX, 38, minuteText);
  } else {
    oled_.drawStr(timeX, 38, timeText);
  }

  // This font contains the Cyrillic glyphs required by the full Russian
  // month names and the fallback text. drawUTF8 performs UTF-8 decoding.
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  const int dateWidth = oled_.getUTF8Width(dateText);
  oled_.drawUTF8(dateWidth < 128 ? (128 - dateWidth) / 2 : 0, 62, dateText);
  presentFrame(millis());
}

void RobotDisplay::showWeather(const WeatherData& data,
                               WeatherStatus status) {
  oled_.clearBuffer();
  oled_.setFont(u8g2_font_6x13_t_cyrillic);

  if (!data.valid) {
    const char* message = "НЕТ ДАННЫХ";
    if (status == WeatherStatus::Loading) {
      message = "ЗАГРУЗКА...";
    } else if (status == WeatherStatus::Error) {
      message = "ОШИБКА СЕТИ";
    }
    const char* title = "ПОГОДА";
    oled_.drawUTF8((128 - oled_.getUTF8Width(title)) / 2, 26, title);
    oled_.drawUTF8((128 - oled_.getUTF8Width(message)) / 2, 48, message);
    presentFrame(millis());
    return;
  }

  drawWeatherIcon(data.weatherCode, data.isDay, 2, 0);

  const int temperature = static_cast<int>(lroundf(data.temperature));
  char temperatureText[8];
  snprintf(temperatureText, sizeof(temperatureText),
           temperature > 0 ? "+%d" : "%d", temperature);
  oled_.setFont(u8g2_font_logisoso20_tf);
  const int temperatureWidth = oled_.getStrWidth(temperatureText);
  const int temperatureX = 124 - temperatureWidth - 8;
  oled_.drawStr(temperatureX, 27, temperatureText);
  // A drawn circle is reliable with every U8g2 font and reads as a degree sign.
  oled_.drawCircle(temperatureX + temperatureWidth + 4, 7, 2);

  const char* description = "ПОГОДА";
  const uint8_t code = data.weatherCode;
  if (code == 0) {
    description = "ЯСНО";
  } else if (code <= 2) {
    description = "МАЛООБЛАЧНО";
  } else if (code == 3) {
    description = "ОБЛАЧНО";
  } else if (code == 45 || code == 48) {
    description = "ТУМАН";
  } else if (code >= 51 && code <= 57) {
    description = "МОРОСЬ";
  } else if ((code >= 61 && code <= 67) ||
             (code >= 80 && code <= 82)) {
    description = "ДОЖДЬ";
  } else if ((code >= 71 && code <= 77) ||
             (code >= 85 && code <= 86)) {
    description = "СНЕГ";
  } else if (code >= 95 && code <= 99) {
    description = "ГРОЗА";
  }

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  oled_.drawUTF8((128 - oled_.getUTF8Width(description)) / 2, 47,
                 description);
  char details[32];
  snprintf(details, sizeof(details), "ВЛ %u%%  ВЕТ %d м/с", data.humidity,
           static_cast<int>(lroundf(data.windSpeed)));
  oled_.setFont(u8g2_font_5x8_t_cyrillic);
  oled_.drawUTF8((128 - oled_.getUTF8Width(details)) / 2, 63, details);
  if (status == WeatherStatus::Stale) {
    oled_.setFont(u8g2_font_6x13_tf);
    oled_.drawStr(122, 11, "!");
  }
  presentFrame(millis());
}

void RobotDisplay::drawSun(int x, int y) {
  const int cx = x + 19;
  const int cy = y + 18;
  oled_.drawDisc(cx, cy, 7);
  oled_.drawLine(cx, y + 2, cx, y + 7);
  oled_.drawLine(cx, y + 29, cx, y + 34);
  oled_.drawLine(x + 3, cy, x + 8, cy);
  oled_.drawLine(x + 30, cy, x + 35, cy);
  oled_.drawLine(x + 7, y + 6, x + 11, y + 10);
  oled_.drawLine(x + 27, y + 26, x + 31, y + 30);
  oled_.drawLine(x + 7, y + 30, x + 11, y + 26);
  oled_.drawLine(x + 27, y + 10, x + 31, y + 6);
}

void RobotDisplay::drawMoon(int x, int y) {
  oled_.drawDisc(x + 18, y + 18, 12);
  oled_.setDrawColor(0);
  oled_.drawDisc(x + 24, y + 13, 11);
  oled_.setDrawColor(1);
}

void RobotDisplay::drawCloud(int x, int y) {
  oled_.drawDisc(x + 13, y + 20, 7);
  oled_.drawDisc(x + 22, y + 15, 10);
  oled_.drawDisc(x + 31, y + 21, 7);
  oled_.drawBox(x + 9, y + 20, 27, 8);
}

void RobotDisplay::drawRain(int x, int y) {
  drawCloud(x, y);
  oled_.drawLine(x + 13, y + 31, x + 10, y + 36);
  oled_.drawLine(x + 23, y + 31, x + 20, y + 36);
  oled_.drawLine(x + 33, y + 31, x + 30, y + 36);
}

void RobotDisplay::drawSnow(int x, int y) {
  drawCloud(x, y);
  for (int offset = 13; offset <= 31; offset += 9) {
    oled_.drawHLine(x + offset - 2, y + 34, 5);
    oled_.drawVLine(x + offset, y + 32, 5);
  }
}

void RobotDisplay::drawThunderstorm(int x, int y) {
  drawCloud(x, y);
  oled_.drawTriangle(x + 23, y + 28, x + 17, y + 35,
                     x + 22, y + 35);
  oled_.drawTriangle(x + 22, y + 33, x + 28, y + 33,
                     x + 20, y + 39);
}

void RobotDisplay::drawFog(int x, int y) {
  for (int row = 0; row < 4; ++row) {
    const int inset = (row % 2) * 4;
    oled_.drawHLine(x + 4 + inset, y + 10 + row * 7, 31 - inset * 2);
  }
}

void RobotDisplay::drawWeatherIcon(uint8_t weatherCode, bool isDay,
                                   int x, int y) {
  if (weatherCode == 0) {
    isDay ? drawSun(x, y) : drawMoon(x, y);
  } else if (weatherCode <= 2) {
    isDay ? drawSun(x - 5, y - 5) : drawMoon(x - 3, y - 4);
    drawCloud(x + 3, y + 7);
  } else if (weatherCode == 45 || weatherCode == 48) {
    drawFog(x, y);
  } else if ((weatherCode >= 51 && weatherCode <= 67) ||
             (weatherCode >= 80 && weatherCode <= 82)) {
    drawRain(x, y);
  } else if ((weatherCode >= 71 && weatherCode <= 77) ||
             (weatherCode >= 85 && weatherCode <= 86)) {
    drawSnow(x, y);
  } else if (weatherCode >= 95 && weatherCode <= 99) {
    drawThunderstorm(x, y);
  } else {
    drawCloud(x, y);
  }
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

void RobotDisplay::drawSoundStatus(bool enabled) {
  // Compact 13x12 speaker icon that fits before the left eye at x=15.
  oled_.drawBox(1, 5, 3, 4);
  oled_.drawLine(4, 5, 8, 2);
  oled_.drawVLine(8, 2, 10);
  oled_.drawLine(8, 11, 4, 8);

  if (enabled) {
    oled_.drawLine(10, 4, 12, 6);
    oled_.drawVLine(12, 6, 2);
    oled_.drawLine(12, 8, 10, 10);
  } else {
    oled_.drawLine(1, 1, 12, 12);
    oled_.drawLine(2, 1, 13, 12);
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
  drawCenteredText(phrases::at(phrases::CURIOUS, phraseVariant));
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
  drawCenteredText(phrases::at(phrases::HAPPY, phraseVariant));
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
    drawCenteredText(phrases::TRANSITION);
    return;
  }

  const unsigned long minutes = roundedMinutes(elapsed);
  if (showDuration && minutes > 0) {
    char message[48];
    snprintf(message, sizeof(message), phrases::at(phrases::CONTENT_DURATION, 0),
             minutes);
    drawCenteredText(message);
    return;
  }

  drawCenteredText(phrases::at(phrases::CONTENT, phraseVariant));
}

void RobotDisplay::drawWaiting(unsigned long now, uint8_t phraseVariant,
                               bool phraseTransitionActive) {
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
  if (phraseTransitionActive) {
    drawCenteredText(phrases::TRANSITION);
    return;
  }
  drawCenteredText(phrases::at(phrases::WAITING, phraseVariant));
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
    drawCenteredText(phrases::TRANSITION);
    return;
  }

  const unsigned long minutes = roundedMinutes(elapsed);
  if (showDuration && minutes > 0) {
    char message[48];
    snprintf(message, sizeof(message), phrases::at(phrases::BORED_DURATION, 0),
             minutes);
    drawCenteredText(message);
    return;
  }

  drawCenteredText(phrases::at(phrases::BORED, phraseVariant));
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
  drawCenteredText(phrases::at(phrases::OFFENDED, phraseVariant));
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
  drawCenteredText(phrases::at(phrases::RECONCILING, phraseVariant));
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
  drawCenteredText(phrases::at(phrases::DROWSY, 0));
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
  drawCenteredText(phrases::at(phrases::SLEEP, 0));
}

void RobotDisplay::drawWake(unsigned long now, unsigned long wakeStartedAt) {
  const unsigned long elapsed = now - wakeStartedAt;
  oled_.setFont(u8g2_font_6x13_t_cyrillic);

  if (elapsed < 300) {
    oled_.drawHLine(15, 26, 28);
    oled_.drawHLine(85, 26, 28);
    drawCenteredText(phrases::at(phrases::WAKE, 0));
  } else if (elapsed < 750) {
    oled_.drawRFrame(15, 13, 28, 26, 7);
    oled_.drawDisc(29, 26, 5);
    oled_.drawHLine(85, 26, 28);
    drawCenteredText(phrases::at(phrases::WAKE, 1));
  } else {
    oled_.drawRFrame(15, 13, 28, 26, 7);
    oled_.drawDisc(29, 26, 5);
    oled_.drawRFrame(85, 13, 28, 26, 7);
    oled_.drawDisc(99, 26, 5);
    oled_.drawCircle(64, 46, 4);
    drawCenteredText(phrases::at(phrases::WAKE, 2));
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
  drawCenteredText(phrases::at(phrases::SURPRISED, 0));
}

void RobotDisplay::drawStartled(unsigned long now, uint8_t phraseVariant) {
  const int8_t jitter = static_cast<int8_t>((now / 55) % 3) - 1;
  const bool showImpactLines = (now / 90) % 2 == 0;

  // A fast horizontal tremble, tense brows and small pupils make this
  // expression visibly different from the softer IMU Surprised animation.
  oled_.drawLine(17 + jitter, 6, 40 + jitter, 11);
  oled_.drawLine(88 + jitter, 11, 111 + jitter, 6);
  oled_.drawRFrame(13 + jitter, 11, 32, 30, 8);
  oled_.drawDisc(29 + jitter, 26, 3);
  oled_.drawRFrame(83 + jitter, 11, 32, 30, 8);
  oled_.drawDisc(99 + jitter, 26, 3);

  // A compact open mouth reads as a sudden gasp instead of surprise.
  oled_.drawRFrame(59 + jitter, 43, 11, 9, 4);
  if (showImpactLines) {
    oled_.drawLine(5, 16, 10, 19);
    oled_.drawLine(5, 25, 10, 25);
    oled_.drawLine(118, 19, 123, 16);
    oled_.drawLine(118, 25, 123, 25);
  }

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  drawCenteredText(phrases::at(phrases::STARTLED, phraseVariant));
}

void RobotDisplay::drawListening(unsigned long now, uint8_t phraseVariant) {
  const int pupilRadius = ((now / 450) % 2 == 0) ? 4 : 5;

  // Slightly taller, forward-looking eyes convey attention without the
  // raised brows, rays and pulsing mouth used by Surprised.
  oled_.drawRFrame(15, 11, 28, 29, 8);
  oled_.drawDisc(29, 26, pupilRadius);
  oled_.drawRFrame(85, 11, 28, 29, 8);
  oled_.drawDisc(99, 26, pupilRadius);
  oled_.drawHLine(19, 8, 20);
  oled_.drawHLine(89, 8, 20);
  oled_.drawLine(59, 48, 63, 46);
  oled_.drawLine(63, 46, 69, 48);

  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  drawCenteredText(phrases::at(phrases::LISTENING, phraseVariant));
}

void RobotDisplay::resetMusicAnimation(unsigned long now) {
  musicAnimation_ = MusicAnimationState{};
  musicAnimation_.startedAt = now;
  musicAnimation_.sourceActive = true;
  musicAnimation_.nextSquintAt =
      now + config::MUSIC_FIRST_SQUINT_DELAY_MS +
      randomDuration(config::MUSIC_SQUINT_MIN_INTERVAL_MS,
                     config::MUSIC_SQUINT_MAX_INTERVAL_MS);
  musicAnimation_.nextNoteAt =
      now + config::MUSIC_FIRST_NOTE_DELAY_MS +
      randomDuration(0, config::MUSIC_NOTE_MAX_INTERVAL_MS -
                            config::MUSIC_NOTE_MIN_INTERVAL_MS);
}

void RobotDisplay::updateMusicAnimation(unsigned long now,
                                        bool sourceActive) {
  if (sourceActive && !musicAnimation_.sourceActive) {
    if (musicAnimation_.startedAt == 0 || musicAnimation_.stoppingAt != 0) {
      resetMusicAnimation(now);
    } else {
      musicAnimation_.sourceActive = true;
    }
  } else if (!sourceActive && musicAnimation_.sourceActive) {
    musicAnimation_.sourceActive = false;
    musicAnimation_.stoppingAt = now;
  }

  if (musicAnimation_.squintActive) {
    const unsigned long duration = config::MUSIC_SQUINT_CLOSE_MS +
                                   musicAnimation_.squintHoldMs +
                                   config::MUSIC_SQUINT_OPEN_MS;
    if (now - musicAnimation_.squintStartedAt >= duration) {
      musicAnimation_.squintActive = false;
      musicAnimation_.nextSquintAt =
          now + randomDuration(config::MUSIC_SQUINT_MIN_INTERVAL_MS,
                               config::MUSIC_SQUINT_MAX_INTERVAL_MS);
    }
  } else if (sourceActive && deadlineReached(now, musicAnimation_.nextSquintAt)) {
    musicAnimation_.squintActive = true;
    musicAnimation_.squintStartedAt = now;
    musicAnimation_.squintHoldMs =
        randomDuration(config::MUSIC_SQUINT_MIN_HOLD_MS,
                       config::MUSIC_SQUINT_MAX_HOLD_MS);
  }

  for (MusicNote& note : musicAnimation_.notes) {
    if (note.active && now - note.startedAt >= note.lifetimeMs) {
      note.active = false;
    }
  }

  if (!sourceActive || !deadlineReached(now, musicAnimation_.nextNoteAt)) {
    return;
  }

  MusicNote* freeNote = nullptr;
  for (MusicNote& note : musicAnimation_.notes) {
    if (!note.active) {
      freeNote = &note;
      break;
    }
  }
  if (freeNote != nullptr) {
    // These three pockets remain clear of the swaying eyes and the equalizer.
    constexpr int8_t NOTE_X[] = {60, 4, 118};
    constexpr int8_t NOTE_Y[] = {22, 16, 16};
    const uint8_t pocket = static_cast<uint8_t>(random(3));
    freeNote->startedAt = now;
    freeNote->lifetimeMs =
        randomDuration(config::MUSIC_NOTE_MIN_LIFETIME_MS,
                       config::MUSIC_NOTE_MAX_LIFETIME_MS);
    freeNote->x = NOTE_X[pocket];
    freeNote->y = NOTE_Y[pocket];
    freeNote->drift = static_cast<int8_t>(random(-2, 3));
    freeNote->rise = static_cast<uint8_t>(random(
        config::MUSIC_NOTE_MIN_RISE_PX, config::MUSIC_NOTE_MAX_RISE_PX + 1));
    freeNote->type = static_cast<uint8_t>(random(2));
    freeNote->active = true;
  }
  musicAnimation_.nextNoteAt =
      now + randomDuration(config::MUSIC_NOTE_MIN_INTERVAL_MS,
                           config::MUSIC_NOTE_MAX_INTERVAL_MS);
}

void RobotDisplay::drawMusicEyes(unsigned long now, int8_t gazeOffset) {
  float envelope = 1.0F;
  const unsigned long age = now - musicAnimation_.startedAt;
  if (age < config::MUSIC_EYE_SWAY_FADE_MS) {
    envelope = static_cast<float>(age) / config::MUSIC_EYE_SWAY_FADE_MS;
  }
  if (musicAnimation_.stoppingAt != 0) {
    const unsigned long stoppingAge = now - musicAnimation_.stoppingAt;
    envelope = stoppingAge >= config::MUSIC_EYE_SWAY_FADE_MS
                   ? 0.0F
                   : 1.0F - static_cast<float>(stoppingAge) /
                                config::MUSIC_EYE_SWAY_FADE_MS;
  }

  const float phase = 2.0F * MUSIC_PI *
                      static_cast<float>(age % config::MUSIC_EYE_SWAY_CYCLE_MS) /
                      config::MUSIC_EYE_SWAY_CYCLE_MS;
  const float angle = config::MUSIC_EYE_SWAY_DEGREES * envelope * sinf(phase) *
                      MUSIC_PI / 180.0F;
  const float sine = sinf(angle);
  const float cosine = cosf(angle);

  float squint = 0.0F;
  if (musicAnimation_.squintActive) {
    const unsigned long elapsed = now - musicAnimation_.squintStartedAt;
    if (elapsed < config::MUSIC_SQUINT_CLOSE_MS) {
      squint = static_cast<float>(elapsed) / config::MUSIC_SQUINT_CLOSE_MS;
    } else if (elapsed < config::MUSIC_SQUINT_CLOSE_MS +
                             musicAnimation_.squintHoldMs) {
      squint = 1.0F;
    } else {
      const unsigned long opening =
          elapsed - config::MUSIC_SQUINT_CLOSE_MS - musicAnimation_.squintHoldMs;
      squint = 1.0F - static_cast<float>(opening) /
                           config::MUSIC_SQUINT_OPEN_MS;
      if (squint < 0.0F) squint = 0.0F;
    }
  }

  const float halfHeight =
      11.0F * (1.0F - squint * (1.0F - config::MUSIC_SQUINT_HEIGHT_RATIO));
  const int beatGaze = (dancingBeatSequence_ & 1U) ? -3 : 3;
  const int gaze = gazeOffset != 0 ? gazeOffset : beatGaze;
  const unsigned long sinceBeat = now - dancingBeatAt_;
  int lift = 0;
  if (sinceBeat < 80) {
    lift = static_cast<int>(sinceBeat * 4 / 80);
  } else if (sinceBeat < 300) {
    lift = static_cast<int>((300 - sinceBeat) * 4 / 220);
  }
  const float centerY = 16.0F - lift;

  if (squint > 0.72F) {
    drawRotatedHappyArc(oled_, 29.0F, centerY, sine, cosine);
    drawRotatedHappyArc(oled_, 99.0F, centerY, sine, cosine);
    return;
  }

  drawRotatedEyeFrame(oled_, 29.0F, centerY, halfHeight, sine, cosine);
  drawRotatedEyeFrame(oled_, 99.0F, centerY, halfHeight, sine, cosine);
  if (squint < 0.58F) {
    PointF leftPupil = rotatePoint(gaze, 0.5F, 29.0F, centerY, sine, cosine);
    PointF rightPupil = rotatePoint(gaze, 0.5F, 99.0F, centerY, sine, cosine);
    oled_.drawDisc(lroundf(leftPupil.x), lroundf(leftPupil.y), 4);
    oled_.drawDisc(lroundf(rightPupil.x), lroundf(rightPupil.y), 4);
  }
}

void RobotDisplay::drawMusicNotes(unsigned long now) {
  for (const MusicNote& note : musicAnimation_.notes) {
    if (!note.active || note.lifetimeMs == 0) continue;
    const unsigned long age = now - note.startedAt;
    const float progress =
        static_cast<float>(age) / static_cast<float>(note.lifetimeMs);
    if (progress >= 1.0F) continue;
    const int x = note.x + lroundf(note.drift * sinf(progress * MUSIC_PI));
    const int y = note.y - lroundf(note.rise * progress);
    const bool shrinking = progress > 0.75F;

    if (shrinking) {
      oled_.drawDisc(x + 2, y + 6, 1);
      oled_.drawVLine(x + 3, y + 1, 5);
      oled_.drawLine(x + 3, y + 1, x + 5, y + 2);
    } else {
      oled_.drawDisc(x + 2, y + 6, 2);
      oled_.drawVLine(x + 4, y, 7);
      oled_.drawLine(x + 4, y, x + 7, y + 2);
      if (note.type != 0) {
        oled_.drawDisc(x + 7, y + 7, 2);
        oled_.drawVLine(x + 8, y + 1, 7);
        oled_.drawHLine(x + 4, y, 5);
      }
    }
  }
}

void RobotDisplay::drawDancing(unsigned long now, int8_t gazeOffset,
                               const uint8_t* bands, uint32_t beatSequence,
                               float beatStrength, bool sourceActive) {
  if (beatSequence != dancingBeatSequence_) {
    dancingBeatSequence_ = beatSequence;
    dancingBeatAt_ = now;
    dancingBeatStrength_ = beatStrength;
  }
  updateMusicAnimation(now, sourceActive);
  drawMusicEyes(now, gazeOffset);
  drawMusicNotes(now);

  constexpr uint8_t barWidth = 13;
  constexpr uint8_t gap = 3;
  constexpr uint8_t startX = 1;
  for (uint8_t band = 0; band < config::MUSIC_BAND_COUNT; ++band) {
    const uint8_t height =
        bands == nullptr ? 0 : (bands[band] > 15 ? 15 : bands[band]);
    if (height > 0) {
      oled_.drawBox(startX + band * (barWidth + gap), 64 - height,
                    barWidth, height);
    }
  }
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
  drawCenteredText(phrases::at(phrases::PICKED_UP, 0));
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
  drawCenteredText(phrases::at(phrases::DIZZY, 0));
}

void RobotDisplay::drawUpsideDown() {
  oled_.drawRFrame(85, 25, 28, 26, 7);
  oled_.drawDisc(99, 38, 5);
  oled_.drawRFrame(15, 25, 28, 26, 7);
  oled_.drawDisc(29, 38, 5);
  oled_.drawCircle(64, 14, 5);
  oled_.setFont(u8g2_font_6x13_t_cyrillic);
  drawCenteredText(phrases::at(phrases::UPSIDE_DOWN, 0));
}

bool RobotDisplay::update(RobotState state, unsigned long now,
                          unsigned long stateStartedAt,
                          uint32_t behaviorEpisodeId, int8_t gazeOffset,
                          const uint8_t* musicBands,
                          uint32_t musicBeatSequence,
                          float musicBeatStrength, bool musicActive) {
  bool phraseChanged =
      (!displayedStateInitialized_ || displayedState_ != state) &&
      state != RobotState::Sleep;
  const bool usesRandomPhrase =
      state == RobotState::Happy || state == RobotState::Offended ||
      state == RobotState::Reconciling;
  if (usesRandomPhrase &&
      (!fastPhraseInitialized_ || fastPhraseState_ != state ||
       fastPhraseEpisodeId_ != behaviorEpisodeId)) {
    fastPhraseState_ = state;
    fastPhraseEpisodeId_ = behaviorEpisodeId;
    fastPhraseVariant_ =
        static_cast<uint8_t>(random(phraseCountFor(state)));
    fastPhraseInitialized_ = true;
    phraseChanged = true;
  }

  const bool enteringListening =
      state == RobotState::Listening &&
      (!displayedStateInitialized_ || displayedState_ != state);
  if (enteringListening) {
    const uint8_t count = phraseCountFor(RobotState::Listening);
    listeningPhraseVariant_ =
        listeningPhraseInitialized_
            ? randomDifferentVariant(listeningPhraseVariant_, count)
            : static_cast<uint8_t>(random(count));
    listeningPhraseInitialized_ = true;
    phraseChanged = true;
  }

  const bool enteringStartled =
      state == RobotState::Startled &&
      (!displayedStateInitialized_ || displayedState_ != state);
  if (enteringStartled) {
    const uint8_t count = phraseCountFor(RobotState::Startled);
    startledPhraseVariant_ =
        startledPhraseInitialized_
            ? randomDifferentVariant(startledPhraseVariant_, count)
            : static_cast<uint8_t>(random(count));
    startledPhraseInitialized_ = true;
    phraseChanged = true;
  }

  const bool usesLongPhrase =
      state == RobotState::Curious || state == RobotState::Waiting ||
      state == RobotState::Bored || state == RobotState::Content;
  if (usesLongPhrase) {
    const bool sameStateEpisode =
        longPhraseInitialized_ && longPhraseState_ == state &&
        longPhraseEpisodeId_ == behaviorEpisodeId;
    const bool variableDurationState =
        state == RobotState::Waiting || state == RobotState::Content ||
        state == RobotState::Bored;

    if (!sameStateEpisode) {
      longPhraseState_ = state;
      longPhraseEpisodeId_ = behaviorEpisodeId;
      longPhraseVariant_ =
          static_cast<uint8_t>(random(phraseCountFor(state)));
      longPhraseShowsDuration_ = false;
      longPhraseTransitionActive_ = false;
      if (variableDurationState) {
        longPhraseChangesAt_ = now + nextPhraseDuration(state);
        if (state != RobotState::Waiting) {
          longPhraseTimeReminderAt_ =
              now + randomDuration(TIME_REMINDER_MIN_MS, TIME_REMINDER_MAX_MS);
        }
      }
      longPhraseInitialized_ = true;
      phraseChanged = true;
    } else if (variableDurationState) {
      if (longPhraseTransitionActive_ &&
          deadlineReached(now, longPhraseTransitionUntil_)) {
        longPhraseTransitionActive_ = false;
        phraseChanged = true;
      }

      if (!longPhraseTransitionActive_ &&
          deadlineReached(now, longPhraseChangesAt_)) {
        const unsigned long elapsed = now - stateStartedAt;
        const bool showDuration =
            state != RobotState::Waiting &&
            deadlineReached(now, longPhraseTimeReminderAt_) &&
            roundedMinutes(elapsed) > 0;
        longPhraseShowsDuration_ = showDuration;
        if (showDuration) {
          longPhraseTimeReminderAt_ =
              now + randomDuration(TIME_REMINDER_MIN_MS,
                                   TIME_REMINDER_MAX_MS);
        } else {
          longPhraseVariant_ = randomDifferentVariant(
              longPhraseVariant_, phraseCountFor(state));
        }

        longPhraseTransitionActive_ = true;
        longPhraseTransitionUntil_ = now + PHRASE_TRANSITION_MS;

        longPhraseChangesAt_ =
            longPhraseTransitionUntil_ + nextPhraseDuration(state);
      }
    }
  }

  oled_.clearBuffer();

  switch (state) {
    case RobotState::Curious:
      drawCurious(now, gazeOffset, longPhraseVariant_);
      break;
    case RobotState::Waiting:
      drawWaiting(now, longPhraseVariant_, longPhraseTransitionActive_);
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
    case RobotState::Startled:
      drawStartled(now, startledPhraseVariant_);
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
    case RobotState::Listening:
      drawListening(now, listeningPhraseVariant_);
      break;
    case RobotState::Dancing:
      drawDancing(now, gazeOffset, musicBands, musicBeatSequence,
                  musicBeatStrength, musicActive);
      break;
    case RobotState::Count:
      break;
  }

  if (soundStatusUntil_ != 0) {
    if (!deadlineReached(now, soundStatusUntil_)) {
      drawSoundStatus(soundStatusEnabled_);
    } else {
      soundStatusUntil_ = 0;
    }
  }

  presentFrame(now);
  if (displayedStateInitialized_ && displayedState_ == RobotState::Dancing &&
      state != RobotState::Dancing) {
    musicAnimation_ = MusicAnimationState{};
  }
  displayedState_ = state;
  displayedStateInitialized_ = true;
  return phraseChanged;
}
