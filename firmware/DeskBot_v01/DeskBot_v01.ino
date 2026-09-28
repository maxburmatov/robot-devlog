#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_VL53L0X.h>

// =====================================
// DESKBOT — СИСТЕМА ЭМОЦИЙ v0.2
// ESP32 + SSD1306 I2C + VL53L0X I2C
// =====================================

// Одна общая шина I2C
#define I2C_SDA 21
#define I2C_SCL 22

// Пороги расстояния (мм)
#define HAPPY_DISTANCE   300
#define CURIOUS_DISTANCE 400

// Интервалы обновления (мс)
#define SENSOR_INTERVAL  100
#define DISPLAY_INTERVAL 50
#define BLINK_INTERVAL   3000
#define BLINK_DURATION   120

// Если объект полностью исчез
#define OBJECT_LOST_TIMEOUT 500

// =====================================
// OLED SSD1306 128x64 I2C
// =====================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// =====================================
// VL53L0X
// =====================================

Adafruit_VL53L0X tof;

// =====================================
// СОСТОЯНИЯ РОБОТА
// =====================================

enum RobotState {
  CURIOUS,
  HAPPY
};

RobotState currentState = CURIOUS;

// =====================================
// ТАЙМЕРЫ
// =====================================

unsigned long lastSensorUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastValidMeasurement = 0;

uint16_t distanceMM = 0;

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

    // Левый глаз
    oled.drawRFrame(15, 13, 28, 26, 7);
    oled.drawDisc(29 + gaze, 26, 5);

    // Правый глаз
    oled.drawRFrame(85, 13, 28, 26, 7);
    oled.drawDisc(99 + gaze, 26, 5);
  }

  // Рот для эмоции Curious
  oled.drawCircle(64, 46, 4);

  // Текст
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

    // Такие же контуры глаз, как в CURIOUS
    oled.drawRFrame(15, 13, 28, 26, 7);
    oled.drawRFrame(85, 13, 28, 26, 7);

    // Сердца вместо зрачков
    drawHeart(29, 23);
    drawHeart(99, 23);
  }

  // Улыбка
  oled.drawLine(48, 43, 52, 47);
  oled.drawLine(52, 47, 58, 50);
  oled.drawLine(58, 50, 64, 51);
  oled.drawLine(64, 51, 70, 50);
  oled.drawLine(70, 50, 76, 47);
  oled.drawLine(76, 47, 80, 43);

  // Текст
  oled.setFont(u8g2_font_6x13_t_cyrillic);
  oled.drawUTF8(23, 63, "Ооо, ты здесь!");
}

// =====================================
// ОБНОВЛЕНИЕ ДИСПЛЕЯ
// =====================================

void updateDisplay(unsigned long now) {

  oled.clearBuffer();

  if (currentState == CURIOUS) {
    drawCurious(now);
  }

  if (currentState == HAPPY) {
    drawHappy(now);
  }

  oled.sendBuffer();
}

// =====================================
// ОБНОВЛЕНИЕ ДАТЧИКА
// =====================================

void updateSensor() {

  VL53L0X_RangingMeasurementData_t measure;

  tof.rangingTest(&measure, false);

  unsigned long now = millis();

  // Некорректное измерение или потеря объекта
  if (measure.RangeStatus != 0) {

    Serial.print("Некорректное измерение. Статус: ");
    Serial.println(measure.RangeStatus);

    if (
      currentState == HAPPY &&
      now - lastValidMeasurement >= OBJECT_LOST_TIMEOUT
    ) {

      currentState = CURIOUS;

      Serial.println(
        "STATE: HAPPY -> CURIOUS (объект потерян)"
      );
    }

    return;
  }

  // Корректное измерение
  distanceMM = measure.RangeMilliMeter;

  lastValidMeasurement = now;

  Serial.print("Расстояние: ");
  Serial.print(distanceMM);
  Serial.println(" mm");

  // Переходы между состояниями
  switch (currentState) {

    case CURIOUS:

      if (distanceMM <= HAPPY_DISTANCE) {

        currentState = HAPPY;

        Serial.println(
          "STATE: CURIOUS -> HAPPY"
        );
      }

      break;

    case HAPPY:

      if (distanceMM >= CURIOUS_DISTANCE) {

        currentState = CURIOUS;

        Serial.println(
          "STATE: HAPPY -> CURIOUS"
        );
      }

      break;
  }
}

// =====================================
// НАСТРОЙКА
// =====================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("СИСТЕМА ЭМОЦИЙ DESKBOT");

  // ===================================
  // ОДНА ОБЩАЯ ШИНА I2C
  // ===================================

  Wire.begin(I2C_SDA, I2C_SCL);

  // Начинаем с безопасной частоты 100 кГц
  Wire.setClock(100000);

  // ===================================
  // OLED
  // ===================================

  // U8g2 использует восьмибитный формат I2C-адреса
  // Адрес OLED = 0x3C
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
  Serial.println("VL53L0X: 0x29");
  Serial.println("STATE: CURIOUS");

  currentState = CURIOUS;

  // Предотвращаем некорректное состояние при запуске
  lastValidMeasurement = millis();

  delay(500);
}

// =====================================
// ОСНОВНОЙ ЦИКЛ
// =====================================

void loop() {

  unsigned long now = millis();

  // Датчик
  if (
    now - lastSensorUpdate >= SENSOR_INTERVAL
  ) {

    lastSensorUpdate = now;

    updateSensor();
  }

  // Анимация дисплея
  if (
    now - lastDisplayUpdate >= DISPLAY_INTERVAL
  ) {

    lastDisplayUpdate = now;

    updateDisplay(now);
  }
}
