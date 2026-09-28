#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_VL53L0X.h>
#include <BH1750.h>

// =====================================
// DESKBOT - EMOTION SYSTEM v0.3
// ESP32 + SSD1306 I2C + VL53L0X + BH1750
// =====================================

// One shared I2C bus
#define I2C_SDA 21
#define I2C_SCL 22

// =====================================
// DISTANCE SETTINGS
// =====================================

#define HAPPY_DISTANCE   300
#define CURIOUS_DISTANCE 400

// =====================================
// LIGHT SETTINGS
// =====================================

#define SLEEP_LUX 10.0
#define WAKE_LUX  20.0

// =====================================
// TIMINGS
// =====================================

#define SENSOR_INTERVAL     100
#define LIGHT_INTERVAL      500
#define DISPLAY_INTERVAL    50
#define BLINK_INTERVAL      3000
#define BLINK_DURATION      120
#define OBJECT_LOST_TIMEOUT 500

// Wake animation
#define WAKE_DURATION 1200

// =====================================
// OLED
// =====================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// =====================================
// SENSORS
// =====================================

Adafruit_VL53L0X tof;
BH1750 lightMeter;

// =====================================
// ROBOT STATES
// =====================================

enum RobotState {
  CURIOUS,
  HAPPY,
  SLEEP,
  WAKE
};

RobotState currentState = CURIOUS;

// =====================================
// TIMERS / VALUES
// =====================================

unsigned long lastSensorUpdate = 0;
unsigned long lastLightUpdate = 0;
unsigned long lastDisplayUpdate = 0;

unsigned long lastValidMeasurement = 0;
unsigned long wakeStartTime = 0;

uint16_t distanceMM = 0;
float lightLux = 0;

// =====================================
// HEART
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
// CURIOUS FACE
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
// HAPPY FACE
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
// SLEEP FACE
// =====================================

void drawSleep(unsigned long now) {

  // Closed eyes
  oled.drawHLine(15, 26, 28);
  oled.drawHLine(85, 26, 28);

  // Relaxed mouth
  oled.drawHLine(59, 46, 10);

  // Animated ZZZ
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
// WAKE FACE
// =====================================

void drawWake(unsigned long now) {

  unsigned long elapsed = now - wakeStartTime;

  oled.setFont(u8g2_font_6x13_t_cyrillic);

  // Stage 1: both eyes closed
  if (elapsed < 300) {

    oled.drawHLine(15, 26, 28);
    oled.drawHLine(85, 26, 28);

    oled.drawUTF8(43, 63, "Ммм...");

  }

  // Stage 2: left eye opens
  else if (elapsed < 750) {

    // Left open
    oled.drawRFrame(15, 13, 28, 26, 7);
    oled.drawDisc(29, 26, 5);

    // Right closed
    oled.drawHLine(85, 26, 28);

    oled.drawUTF8(34, 63, "Просыпаюсь...");

  }

  // Stage 3: both eyes open
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
// DISPLAY UPDATE
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
// LIGHT SENSOR
// =====================================

void updateLightSensor() {

  float lux = lightMeter.readLightLevel();

  if (lux < 0) {
    Serial.println("BH1750 read error");
    return;
  }

  lightLux = lux;

  Serial.print("Light: ");
  Serial.print(lightLux, 1);
  Serial.println(" lx");

  // ===================================
  // GO TO SLEEP
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
  // WAKE UP
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
// DISTANCE SENSOR
// =====================================

void updateSensor() {

  // Distance reactions are disabled
  // while robot sleeps or wakes up
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
        "STATE: HAPPY -> CURIOUS (object lost)"
      );
    }
  }
}

// =====================================
// WAKE STATE UPDATE
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
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("DESKBOT EMOTION SYSTEM v0.3");

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

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(37, 25, "DESKBOT");
  oled.drawStr(32, 45, "Starting...");

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

    Serial.println("BH1750 NOT FOUND");

  } else {

    Serial.println("BH1750 initialized at 0x23");
  }

  // ===================================
  // VL53L0X
  // ===================================

  if (!tof.begin(0x29, false, &Wire)) {

    Serial.println("VL53L0X NOT FOUND");

    oled.clearBuffer();

    oled.setFont(u8g2_font_6x12_tf);

    oled.drawStr(23, 25, "TOF ERROR");

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
// MAIN LOOP
// =====================================

void loop() {

  unsigned long now = millis();

  // ===================================
  // LIGHT SENSOR
  // ===================================

  if (
    now - lastLightUpdate >= LIGHT_INTERVAL
  ) {

    lastLightUpdate = now;

    updateLightSensor();
  }

  // ===================================
  // WAKE ANIMATION LOGIC
  // ===================================

  updateWakeState(now);

  // ===================================
  // DISTANCE SENSOR
  // ===================================

  if (
    now - lastSensorUpdate >= SENSOR_INTERVAL
  ) {

    lastSensorUpdate = now;

    updateSensor();
  }

  // ===================================
  // DISPLAY
  // ===================================

  if (
    now - lastDisplayUpdate >= DISPLAY_INTERVAL
  ) {

    lastDisplayUpdate = now;

    updateDisplay(now);
  }
}