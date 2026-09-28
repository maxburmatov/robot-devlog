#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_VL53L0X.h>
#include <BH1750.h>

// =====================================
// DESKBOT — СИСТЕМА ЭМОЦИЙ v0.3
// ESP32 + SSD1306 I2C + VL53L0X + BH1750
// =====================================

// Одна общая шина I2C
#define I2C_SDA 21
#define I2C_SCL 22

// =====================================
// НАСТРОЙКИ РАССТОЯНИЯ
// =====================================

#define HAPPY_DISTANCE   300
#define CURIOUS_DISTANCE 400

// =====================================
// НАСТРОЙКИ ОСВЕЩЁННОСТИ
// =====================================

#define SLEEP_LUX 10.0
#define WAKE_LUX  20.0

// =====================================
// ВРЕМЕННЫЕ ПАРАМЕТРЫ
// =====================================

#define SENSOR_INTERVAL     100
#define LIGHT_INTERVAL      500
#define DISPLAY_INTERVAL    50
#define BLINK_INTERVAL      3000
#define BLINK_DURATION      120
#define OBJECT_LOST_TIMEOUT 500

// Анимация пробуждения
#define WAKE_DURATION 1200

// =====================================
// OLED
// =====================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// =====================================
// ДАТЧИКИ
// =====================================

Adafruit_VL53L0X tof;
BH1750 lightMeter;

// =====================================
// СОСТОЯНИЯ РОБОТА
// =====================================

enum RobotState {
  CURIOUS,
  HAPPY,
  SLEEP,
  WAKE
};

RobotState currentState = CURIOUS;

// =====================================
// ТАЙМЕРЫ И ЗНАЧЕНИЯ
// =====================================

unsigned long lastSensorUpdate = 0;
unsigned long lastLightUpdate = 0;
unsigned long lastDisplayUpdate = 0;

unsigned long lastValidMeasurement = 0;
unsigned long wakeStartTime = 0;

uint16_t distanceMM = 0;
float lightLux = 0;

// =====================================
// СЕРДЦЕ
// =====================================

void drawHeart(int cx, int cy) {

  oled.drawDisc(cx - 4, cy - 2, 4);
  oled.drawDisc(cx + 4, cy - 2, 4);

  for (int i = 0; i < 9; i++) {

    int left = cx - 8 + i;
    int width = 17 - i * 2;

    if (width > 0) {
      oled.drawHLine(left, cy + i, width);
    }
  }
}

// =====================================
// ЭМОЦИЯ CURIOUS
// =====================================

void drawCurious(unsigned long now) {

  int gaze = 0;

  unsigned long phase = (now / 900) % 4;

  if (phase == 0) gaze = -4;
  if (phase == 1) gaze = 0;
  if (phase == 2) gaze = 4;
  if (phase == 3) gaze = 0;

  bool blinking =
    (now % BLINK_INTERVAL) < BLINK_DURATION;

  if (blinking) {

    oled.drawHLine(15, 26, 28);
    oled.drawHLine(85, 26, 28);

  } else {

    oled.drawRFrame(15, 13, 28, 26, 7);
    oled.drawDisc(29 + gaze, 26, 5);

    oled.drawRFrame(85, 13, 28, 26, 7);
    oled.drawDisc(99 + gaze, 26, 5);
  }

  oled.drawCircle(64, 46, 4);

  oled.setFont(u8g2_font_6x13_t_cyrillic);
  oled.drawUTF8(28, 63, "Наблюдаю...");
}

// =====================================
// ЭМОЦИЯ HAPPY
// =====================================

void drawHappy(unsigned long now) {

  bool blinking =
    (now % BLINK_INTERVAL) < BLINK_DURATION;

  if (blinking) {

    oled.drawHLine(15, 26, 28);
    oled.drawHLine(85, 26, 28);

  } else {

    oled.drawRFrame(15, 13, 28, 26, 7);
    oled.drawRFrame(85, 13, 28, 26, 7);

    drawHeart(29, 23);
    drawHeart(99, 23);
  }

  oled.drawLine(48, 43, 52, 47);
  oled.drawLine(52, 47, 58, 50);
  oled.drawLine(58, 50, 64, 51);
  oled.drawLine(64, 51, 70, 50);
  oled.drawLine(70, 50, 76, 47);
  oled.drawLine(76, 47, 80, 43);

  oled.setFont(u8g2_font_6x13_t_cyrillic);
  oled.drawUTF8(23, 63, "Ооо, ты здесь!");
}

// =====================================
// ЭМОЦИЯ SLEEP
// =====================================

void drawSleep(unsigned long now) {

  // Закрытые глаза
  oled.drawHLine(15, 26, 28);
  oled.drawHLine(85, 26, 28);

  // Расслабленный рот
  oled.drawHLine(59, 46, 10);

  // Анимированные ZZZ
  oled.setFont(u8g2_font_6x13_tf);

  unsigned long phase = (now / 600) % 3;

  if (phase >= 0) {
    oled.drawStr(99, 20, "Z");
  }

  if (phase >= 1) {
    oled.drawStr(108, 13, "Z");
  }

  if (phase >= 2) {
    oled.drawStr(117, 7, "Z");
  }

  oled.setFont(u8g2_font_6x13_t_cyrillic);
  oled.drawUTF8(42, 63, "Сплю...");
}

// =====================================
// ЭМОЦИЯ WAKE
// =====================================

void drawWake(unsigned long now) {

  unsigned long elapsed = now - wakeStartTime;

  oled.setFont(u8g2_font_6x13_t_cyrillic);

  // Этап 1: оба глаза закрыты
  if (elapsed < 300) {

    oled.drawHLine(15, 26, 28);
    oled.drawHLine(85, 26, 28);

    oled.drawUTF8(43, 63, "Ммм...");

  }

  // Этап 2: открывается левый глаз
  else if (elapsed < 750) {

    // Левый глаз открыт
    oled.drawRFrame(15, 13, 28, 26, 7);
    oled.drawDisc(29, 26, 5);

    // Правый глаз закрыт
    oled.drawHLine(85, 26, 28);

    oled.drawUTF8(34, 63, "Просыпаюсь...");

  }

  // Этап 3: оба глаза открыты
  else {

    oled.drawRFrame(15, 13, 28, 26, 7);
    oled.drawDisc(29, 26, 5);

    oled.drawRFrame(85, 13, 28, 26, 7);
    oled.drawDisc(99, 26, 5);

    oled.drawCircle(64, 46, 4);

    oled.drawUTF8(43, 63, "Ага...");
  }
}

// =====================================
// ОБНОВЛЕНИЕ ДИСПЛЕЯ
// =====================================

void updateDisplay(unsigned long now) {

  oled.clearBuffer();

  switch (currentState) {

    case CURIOUS:
      drawCurious(now);
      break;

    case HAPPY:
      drawHappy(now);
      break;

    case SLEEP:
      drawSleep(now);
      break;

    case WAKE:
      drawWake(now);
      break;
  }

  oled.sendBuffer();
}

// =====================================
// ДАТЧИК ОСВЕЩЁННОСТИ
// =====================================

void updateLightSensor() {

  float lux = lightMeter.readLightLevel();

  if (lux < 0) {
    Serial.println("Ошибка чтения BH1750");
    return;
  }

  lightLux = lux;

  Serial.print("Освещённость: ");
  Serial.print(lightLux, 1);
  Serial.println(" lx");

  // ===================================
  // ПЕРЕХОД КО СНУ
  // ===================================

  if (
    currentState != SLEEP &&
    currentState != WAKE &&
    lightLux < SLEEP_LUX
  ) {

    currentState = SLEEP;

    Serial.println("STATE: -> SLEEP");

    return;
  }

  // ===================================
  // ПРОБУЖДЕНИЕ
  // ===================================

  if (
    currentState == SLEEP &&
    lightLux > WAKE_LUX
  ) {

    currentState = WAKE;
    wakeStartTime = millis();

    Serial.println("STATE: SLEEP -> WAKE");
  }
}

// =====================================
// ДАТЧИК РАССТОЯНИЯ
// =====================================

void updateSensor() {

  // Реакции на расстояние отключены,
  // пока робот спит или просыпается
  if (
    currentState == SLEEP ||
    currentState == WAKE
  ) {
    return;
  }

  VL53L0X_RangingMeasurementData_t measure;

  tof.rangingTest(&measure, false);

  if (measure.RangeStatus == 0) {

    distanceMM = measure.RangeMilliMeter;
    lastValidMeasurement = millis();

    if (
      currentState == CURIOUS &&
      distanceMM <= HAPPY_DISTANCE
    ) {

      currentState = HAPPY;

      Serial.println("STATE: CURIOUS -> HAPPY");
    }

    else if (
      currentState == HAPPY &&
      distanceMM >= CURIOUS_DISTANCE
    ) {

      currentState = CURIOUS;

      Serial.println("STATE: HAPPY -> CURIOUS");
    }

  } else {

    if (
      currentState == HAPPY &&
      millis() - lastValidMeasurement >= OBJECT_LOST_TIMEOUT
    ) {

      currentState = CURIOUS;

      Serial.println(
        "STATE: HAPPY -> CURIOUS (объект потерян)"
      );
    }
  }
}

// =====================================
// ОБНОВЛЕНИЕ СОСТОЯНИЯ ПРОБУЖДЕНИЯ
// =====================================

void updateWakeState(unsigned long now) {

  if (
    currentState == WAKE &&
    now - wakeStartTime >= WAKE_DURATION
  ) {

    currentState = CURIOUS;

    Serial.println("STATE: WAKE -> CURIOUS");
  }
}

// =====================================
// НАСТРОЙКА
// =====================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("СИСТЕМА ЭМОЦИЙ DESKBOT v0.3");

  // ===================================
  // I2C
  // ===================================

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // ===================================
  // OLED
  // ===================================

  oled.setI2CAddress(0x3C * 2);
  oled.begin();
  oled.enableUTF8Print();

  oled.clearBuffer();

  oled.setFont(u8g2_font_6x13_t_cyrillic);

  oled.drawStr(37, 25, "DESKBOT");
  oled.drawUTF8(40, 45, "Запуск...");

  oled.sendBuffer();

  delay(1000);

  // ===================================
  // BH1750
  // ===================================

  if (!lightMeter.begin(
        BH1750::CONTINUOUS_HIGH_RES_MODE,
        0x23,
        &Wire
      )) {

    Serial.println("BH1750 НЕ НАЙДЕН");

  } else {

    Serial.println("BH1750 инициализирован по адресу 0x23");
  }

  // ===================================
  // VL53L0X
  // ===================================

  if (!tof.begin(0x29, false, &Wire)) {

    Serial.println("VL53L0X НЕ НАЙДЕН");

    oled.clearBuffer();

    oled.setFont(u8g2_font_6x13_t_cyrillic);

    oled.drawUTF8(31, 25, "Ошибка ToF");

    oled.sendBuffer();

    while (true) {
      delay(100);
    }
  }

  Serial.println("OLED: 0x3C");
  Serial.println("BH1750: 0x23");
  Serial.println("VL53L0X: 0x29");

  currentState = CURIOUS;

  lastValidMeasurement = millis();

  Serial.println("STATE: CURIOUS");

  delay(500);
}

// =====================================
// ОСНОВНОЙ ЦИКЛ
// =====================================

void loop() {

  unsigned long now = millis();

  // ===================================
  // ДАТЧИК ОСВЕЩЁННОСТИ
  // ===================================

  if (
    now - lastLightUpdate >= LIGHT_INTERVAL
  ) {

    lastLightUpdate = now;

    updateLightSensor();
  }

  // ===================================
  // ЛОГИКА АНИМАЦИИ ПРОБУЖДЕНИЯ
  // ===================================

  updateWakeState(now);

  // ===================================
  // ДАТЧИК РАССТОЯНИЯ
  // ===================================

  if (
    now - lastSensorUpdate >= SENSOR_INTERVAL
  ) {

    lastSensorUpdate = now;

    updateSensor();
  }

  // ===================================
  // ДИСПЛЕЙ
  // ===================================

  if (
    now - lastDisplayUpdate >= DISPLAY_INTERVAL
  ) {

    lastDisplayUpdate = now;

    updateDisplay(now);
  }
}
