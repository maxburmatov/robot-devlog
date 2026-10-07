#pragma once

#include <Arduino.h>

namespace config {

// ========================= I2C =========================

// Номер GPIO, подключённого к линии данных SDA общей шины I2C.
constexpr uint8_t I2C_SDA_PIN = 8;

// Номер GPIO, подключённого к линии тактирования SCL общей шины I2C.
constexpr uint8_t I2C_SCL_PIN = 9;

// Частота общей шины I2C в герцах.
constexpr uint32_t I2C_FREQUENCY_HZ = 100000;

// Семибитный I2C-адрес OLED-дисплея SSD1306.
constexpr uint8_t OLED_ADDRESS = 0x3C;

// Семибитный I2C-адрес датчика освещённости BH1750.
constexpr uint8_t LIGHT_SENSOR_ADDRESS = 0x23;

// Семибитный I2C-адрес датчика расстояния VL53L0X.
constexpr uint8_t DISTANCE_SENSOR_ADDRESS = 0x29;

// Семибитный I2C-адрес акселерометра и гироскопа LSM6DS3.
constexpr uint8_t IMU_ADDRESS = 0x6B;

// Seven-bit I2C address of the optional APDS-9960 gesture sensor.
constexpr uint8_t GESTURE_SENSOR_ADDRESS = 0x39;

// ========================= AUDIO =========================

// Set to false to disable I2S initialization and all sound reactions.
constexpr bool AUDIO_ENABLED = true;

constexpr uint8_t I2S_BCLK_PIN = 16;
constexpr uint8_t I2S_LRC_PIN = 17;
constexpr uint8_t I2S_DOUT_PIN = 18;
constexpr uint32_t AUDIO_SAMPLE_RATE = 22050;
constexpr float AUDIO_VOLUME = 0.50F;

// A short DMA block lets a newly displayed phrase replace the previous sound
// quickly. Playback runs in its own task, so sensors and display stay responsive.
constexpr size_t AUDIO_DMA_BUFFER_SAMPLES = 128;
constexpr int AUDIO_DMA_BUFFER_COUNT = 8;
constexpr uint32_t AUDIO_TASK_STACK_WORDS = 4096;
constexpr UBaseType_t AUDIO_TASK_PRIORITY = 1;

// ===================== ПОРОГИ СОСТОЯНИЙ =====================

// Расстояние в миллиметрах, не дальше которого включается состояние Happy.
constexpr uint16_t HAPPY_DISTANCE_MM = 600;

// Расстояние в миллиметрах, начиная с которого Happy сменяется на Curious.
constexpr uint16_t CURIOUS_DISTANCE_MM = 700;

// Long-range preset увеличивает шанс получать valid samples от руки в зоне
// 600–700 мм. Это режим библиотеки VL53L0X, а не расширение до нескольких метров.
constexpr bool TOF_LONG_RANGE_MODE = true;

// Количество последовательных замеров по одну сторону порога до смены
// состояния. Одиночный выброс ToF не должен менять эмоцию.
constexpr uint8_t DISTANCE_CONFIRM_SAMPLES = 3;

// Освещённость в люксах, ниже которой робот переходит в Sleep.
constexpr float SLEEP_LUX = 10.0F;

// Освещённость в люксах, выше которой спящий робот начинает просыпаться.
constexpr float WAKE_LUX = 20.0F;

// ======================== ИНТЕРВАЛЫ ========================

// Период между измерениями расстояния VL53L0X в миллисекундах.
constexpr unsigned long SENSOR_INTERVAL_MS = 100;

// Период диагностического вывода расстояния и RangeStatus.
constexpr unsigned long DISTANCE_LOG_INTERVAL_MS = 500;

// Период между измерениями освещённости BH1750 в миллисекундах.
constexpr unsigned long LIGHT_INTERVAL_MS = 500;

// Период перерисовки OLED-дисплея в миллисекундах.
constexpr unsigned long DISPLAY_INTERVAL_MS = 50;

// Период чтения акселерометра и гироскопа LSM6DS3 в миллисекундах.
constexpr unsigned long IMU_INTERVAL_MS = 20;

// APDS-9960 polling period. The FIFO retains samples between polls, while the
// interval prevents the optional sensor from monopolizing the shared bus.
constexpr unsigned long GESTURE_INTERVAL_MS = 20;

// ====================== WI-FI / SYSTEM TIME ======================

// POSIX TZ syntax uses the opposite sign: MSK-3 means UTC+3, no DST.
constexpr char TIMEZONE[] = "MSK-3";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.google.com";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr unsigned long TIME_SYNC_TIMEOUT_MS = 30000;
constexpr unsigned long TIME_RETRY_INTERVAL_MS = 300000;

// ============================ WEATHER ============================

constexpr unsigned long WEATHER_UPDATE_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long WEATHER_RETRY_INTERVAL_MS = 60UL * 1000UL;
constexpr uint16_t WEATHER_CONNECT_TIMEOUT_MS = 15000;
constexpr uint16_t WEATHER_RESPONSE_TIMEOUT_MS = 15000;
constexpr uint8_t WEATHER_TLS_HANDSHAKE_TIMEOUT_SECONDS = 15;
constexpr unsigned long WEATHER_WIFI_SETTLE_MS = 3000;
constexpr bool WEATHER_ALLOW_HTTP_FALLBACK = true;
constexpr uint32_t WEATHER_TASK_STACK_WORDS = 6144;
constexpr UBaseType_t WEATHER_TASK_PRIORITY = 1;

// Период вывода диагностических показаний IMU в Serial Monitor, в мс.
constexpr unsigned long IMU_LOG_INTERVAL_MS = 250;

// Интервал между началами автоматического моргания в миллисекундах.
constexpr unsigned long BLINK_INTERVAL_MS = 3000;

// Продолжительность закрытого положения глаз при моргании, в мс.
constexpr unsigned long BLINK_DURATION_MS = 120;

// Время без корректного замера дистанции до выхода из Happy, в мс.
constexpr unsigned long OBJECT_LOST_TIMEOUT_MS = 1200;

// Время отсутствия от подтверждённого ухода до перехода Waiting -> Bored.
constexpr unsigned long BOREDOM_BORED_MS = 12000;

// Короткая фаза распознавания Curious перед Happy или Offended.
constexpr unsigned long CURIOUS_REACTION_MS = 800;

// Продолжительность демонстративной обиды после возвращения из Bored.
constexpr unsigned long BOREDOM_OFFENDED_MS = 2500;

// Продолжительность перехода от Offended к спокойному Content.
constexpr unsigned long BOREDOM_RECONCILE_MS = 1200;

// Продолжительность событийной радости перед спокойным Content.
constexpr unsigned long HAPPY_DURATION_MS = 2200;

// Общая продолжительность анимации пробуждения в миллисекундах.
constexpr unsigned long WAKE_DURATION_MS = 1200;

// Минимальное время бодрствования после любого пробуждения, даже в темноте.
constexpr unsigned long AWAKE_AFTER_WAKE_MS = 10000;

// Продолжительность зевка перед переходом в состояние сна, в миллисекундах.
constexpr unsigned long DROWSY_DURATION_MS = 2600;

// ===================== РАСПОЗНАВАНИЕ ДВИЖЕНИЯ =====================

// Минимальное отклонение полного ускорения от 1 g для фиксации движения.
constexpr float IMU_MOVE_ACCEL_DELTA_G = 0.07F;

// Минимальная угловая скорость в градусах/с для фиксации движения.
constexpr float IMU_MOVE_GYRO_DPS = 35.0F;

// Отклонение полного ускорения от 1 g, считающееся встряхиванием.
constexpr float IMU_SHAKE_ACCEL_DELTA_G = 0.65F;

// Угловая скорость в градусах/с, считающаяся встряхиванием.
constexpr float IMU_SHAKE_GYRO_DPS = 220.0F;

// Порог ориентации для входа в состояние переворота.
constexpr float IMU_UPSIDE_DOWN_DOT = -0.55F;

// Порог ориентации для выхода из перевёрнутого состояния.
constexpr float IMU_UPRIGHT_DOT = 0.10F;

// Изменение ускорения по оси X в g, необходимое для смещения взгляда.
constexpr float IMU_GAZE_TILT_G = 0.18F;

// Направление реакции глаз: 1 — обычное, -1 — поменять стороны местами.
constexpr int IMU_GAZE_X_SIGN = -1;

// Минимальное время покоя перед распознаванием нового эпизода движения.
constexpr unsigned long IMU_REST_BEFORE_MOVE_MS = 600;

// Движение должно продолжаться столько времени, чтобы считаться поднятием,
// а не одиночным толчком корпуса.
constexpr unsigned long IMU_PICKUP_CONFIRM_MS = 160;

// Допустимый короткий разрыв между samples движения во время поднятия.
constexpr unsigned long IMU_PICKUP_MOTION_GAP_MS = 180;

// Защита от повторного события поднятия в рамках одного движения.
constexpr unsigned long IMU_PICKUP_COOLDOWN_MS = 2000;

// Защита от повторного срабатывания встряхивания, в миллисекундах.
constexpr unsigned long IMU_SHAKE_COOLDOWN_MS = 800;

// Продолжительность удивлённой эмоции после короткого движения.
constexpr unsigned long SURPRISED_DURATION_MS = 900;

// Пауза после постановки, за которую ToF и корпус успевают стабилизироваться.
constexpr unsigned long PUT_DOWN_SETTLE_MS = 500;

// Продолжительность головокружения после встряхивания, в миллисекундах.
constexpr unsigned long DIZZY_DURATION_MS = 1500;

}  // пространство имён config
