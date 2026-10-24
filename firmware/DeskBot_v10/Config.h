#pragma once

#include <Arduino.h>

namespace config {

// ========================= I2C =========================

// Номер GPIO, подключённого к линии данных SDA общей шины I2C.
constexpr uint8_t I2C_SDA_PIN = 8;

// Номер GPIO, подключённого к линии тактирования SCL общей шины I2C.
constexpr uint8_t I2C_SCL_PIN = 9;

// Частота общей шины I2C в герцах.
// Every device on the shared bus (SSD1306, BH1750, VL53L0X, LSM6DS3 and
// APDS-9960) supports Fast-mode. 400 kHz keeps the gesture FIFO responsive
// while the display and blocking ToF transactions use the same bus.
constexpr uint32_t I2C_FREQUENCY_HZ = 400000;

// Семибитный I2C-адрес OLED-дисплея SSD1306.
constexpr uint8_t OLED_ADDRESS = 0x3C;

// Семибитный I2C-адрес датчика освещённости BH1750.
constexpr uint8_t LIGHT_SENSOR_ADDRESS = 0x23;

// Семибитный I2C-адрес датчика расстояния VL53L0X.
constexpr uint8_t DISTANCE_SENSOR_ADDRESS = 0x29;

// Семибитный I2C-адрес акселерометра и гироскопа LSM6DS3.
constexpr uint8_t IMU_ADDRESS = 0x6B;

// v0.10 does not require an IMU. Keep the inherited motion subsystem compiled
// but skip probing and polling the physically absent LSM6DS3.
constexpr bool IMU_ENABLED = false;

// Seven-bit I2C address of the optional APDS-9960 gesture sensor.
constexpr uint8_t GESTURE_SENSOR_ADDRESS = 0x39;

// Raw sensor telemetry is intentionally opt-in. When false, Serial contains
// only sensor events, state transitions, initialization results and errors.
constexpr bool SENSOR_TELEMETRY_DEBUG = false;

// ========================= NECK SERVOS =========================

// The first physical run must use these deliberately narrow limits. Expand
// them only after checking the assembled bracket and cable slack.
constexpr bool NECK_ENABLED = true;
constexpr uint8_t NECK_YAW_PIN = 13;
constexpr uint8_t NECK_PITCH_PIN = 14;
constexpr uint32_t NECK_PWM_FREQUENCY_HZ = 50;
// ESP32-S3 LEDC timers support up to 14-bit duty resolution. Requesting 16
// bits makes ledcAttach() fail and leaves the neck unavailable.
constexpr uint8_t NECK_PWM_RESOLUTION_BITS = 14;

constexpr uint16_t NECK_YAW_MIN_PULSE_US = 500;
constexpr uint16_t NECK_YAW_MAX_PULSE_US = 2500;
constexpr uint16_t NECK_PITCH_MIN_PULSE_US = 500;
constexpr uint16_t NECK_PITCH_MAX_PULSE_US = 2500;

constexpr float NECK_YAW_CENTER_DEG = 90.0F;
constexpr float NECK_YAW_MIN_DEG = 50.0F;
constexpr float NECK_YAW_MAX_DEG = 130.0F;
constexpr bool NECK_YAW_INVERTED = false;

constexpr float NECK_PITCH_CENTER_DEG = 105.0F;
constexpr float NECK_PITCH_MIN_DEG = 85.0F;
constexpr float NECK_PITCH_MAX_DEG = 125.0F;
constexpr bool NECK_PITCH_INVERTED = true;

constexpr float NECK_DEFAULT_SPEED_DEG_PER_SEC = 45.0F;
constexpr uint32_t NECK_UPDATE_INTERVAL_MS = 20;
// Keep servo interpolation independent from blocking work in loop(). The neck
// task is the only runtime owner of the LEDC outputs; callers submit commands
// through a one-slot latest-command mailbox.
constexpr uint32_t NECK_TASK_STACK_BYTES = 4096;
constexpr UBaseType_t NECK_TASK_PRIORITY = 2;
constexpr BaseType_t NECK_TASK_CORE = 1;

// Zero keeps PWM attached so the head cannot fall under its own weight. Set a
// non-zero timeout only after verifying that this is mechanically safe.
constexpr uint32_t NECK_HOLD_TIMEOUT_MS = 0;

// ====================== AUTOMATIC NECK BEHAVIOR ======================

constexpr bool NECK_BEHAVIOR_ENABLED_BY_DEFAULT = true;
constexpr uint32_t NECK_MANUAL_OVERRIDE_MS = 15000;

constexpr float NECK_POSE_SPEED_DEG_PER_SEC = 50.0F;
constexpr float NECK_REACTION_SPEED_DEG_PER_SEC = 75.0F;
constexpr float NECK_IDLE_SPEED_DEG_PER_SEC = 50.0F;
constexpr float NECK_DANCE_SPEED_DEG_PER_SEC = 120.0F;

constexpr uint32_t NECK_IDLE_MIN_INTERVAL_MS = 3500;
constexpr uint32_t NECK_IDLE_MAX_INTERVAL_MS = 8000;
constexpr uint32_t NECK_IDLE_RETURN_DELAY_MS = 1200;
constexpr float NECK_IDLE_YAW_AMPLITUDE_DEG = 8.0F;
constexpr float NECK_IDLE_PITCH_AMPLITUDE_DEG = 4.0F;

constexpr float NECK_WAKE_PITCH_OFFSET_DEG = 12.0F;
constexpr float NECK_ATTENTION_PITCH_OFFSET_DEG = 5.0F;
constexpr float NECK_DANCE_YAW_AMPLITUDE_DEG = 14.0F;
constexpr float NECK_DANCE_PITCH_AMPLITUDE_DEG = 5.0F;
constexpr uint32_t NECK_MANUAL_DANCE_STEP_MS = 420;

// ======================= TOF GAZE TRACKING =======================

constexpr bool GAZE_TRACKING_ENABLED_BY_DEFAULT = true;
constexpr float GAZE_SENSOR_YAW_OFFSET_DEG = 0.0F;

constexpr float GAZE_SCAN_YAW_DEG[] = {
    65.0F, 77.5F, 90.0F, 102.5F, 115.0F,
};
constexpr size_t GAZE_SCAN_POINT_COUNT = 5;
static_assert(GAZE_SCAN_POINT_COUNT ==
                  sizeof(GAZE_SCAN_YAW_DEG) / sizeof(GAZE_SCAN_YAW_DEG[0]),
              "GAZE_SCAN_POINT_COUNT must match GAZE_SCAN_YAW_DEG");
constexpr float GAZE_SCAN_SPEED_DEG_PER_SEC = 60.0F;
constexpr uint32_t GAZE_SERVO_SETTLE_MS = 200;
constexpr uint8_t GAZE_SAMPLES_PER_POINT = 3;

constexpr uint16_t GAZE_TARGET_MAX_DISTANCE_MM = 700;
constexpr uint16_t GAZE_DISTANCE_HYSTERESIS_MM = 80;
constexpr uint8_t GAZE_REQUIRED_VALID_SAMPLES = 2;
constexpr uint8_t GAZE_LOST_CONFIRM_SAMPLES = 3;

constexpr float GAZE_LOCAL_PROBE_DEG = 6.0F;
constexpr float GAZE_LOCAL_REACQUIRE_DEG = 12.0F;
constexpr float GAZE_MIN_YAW_CHANGE_DEG = 3.0F;
// A locked target is continuously observed along the current optical axis, so
// a moving yaw probe is only needed occasionally or after confirmed loss.
constexpr uint32_t GAZE_TRACK_INTERVAL_MS = 6000;
constexpr uint32_t GAZE_RESCAN_INTERVAL_MS = 10000;
constexpr uint32_t GAZE_TARGET_MEMORY_MS = 5000;

// Small pitch glances keep the robot alive without constantly sweeping past a
// person. ToF loss counting is suspended until the head returns to centre.
constexpr float GAZE_PITCH_GLANCE_DEG = 3.0F;
constexpr float GAZE_PITCH_GLANCE_SPEED_DEG_PER_SEC = 35.0F;
constexpr uint32_t GAZE_PITCH_GLANCE_HOLD_MS = 500;
constexpr uint32_t GAZE_PITCH_GLANCE_MIN_INTERVAL_MS = 8000;
constexpr uint32_t GAZE_PITCH_GLANCE_MAX_INTERVAL_MS = 15000;

// Bored is an active search state: start with a short hesitation and then
// repeat full scans at a calm, slightly random cadence.
constexpr uint32_t GAZE_BORED_FIRST_SCAN_MIN_MS = 2000;
constexpr uint32_t GAZE_BORED_FIRST_SCAN_MAX_MS = 4000;
constexpr uint32_t GAZE_BORED_RESCAN_MIN_MS = 15000;
constexpr uint32_t GAZE_BORED_RESCAN_MAX_MS = 30000;

// The commanded/final yaw must agree before samples can belong to a point.
constexpr float GAZE_YAW_TOLERANCE_DEG = 1.0F;

// ========================= AUDIO =========================

// Select one isolated, event-driven diagnostic target. In isolated modes the
// speaker and unrelated audio consumers are disabled, and loop() skips normal
// sensor/UI work after servicing the selected audio path.
enum class AudioTestMode : uint8_t {
  Normal,
  SoundEvents,
  MusicEvents,
  VoiceEvents,
  DatasetRecorder,
  FullPerformance,
};

constexpr AudioTestMode AUDIO_TEST_MODE = AudioTestMode::FullPerformance;
constexpr bool AUDIO_ISOLATED_TEST =
    AUDIO_TEST_MODE == AudioTestMode::SoundEvents ||
    AUDIO_TEST_MODE == AudioTestMode::MusicEvents ||
    AUDIO_TEST_MODE == AudioTestMode::VoiceEvents ||
    AUDIO_TEST_MODE == AudioTestMode::DatasetRecorder;
constexpr bool AUDIO_PERFORMANCE_DEBUG =
    AUDIO_TEST_MODE == AudioTestMode::FullPerformance;

// Keep the speaker enabled during the full-system performance test. Isolated
// input tests still mute it so playback cannot contaminate the INMP441 signal.
constexpr bool AUDIO_ENABLED =
    AUDIO_TEST_MODE == AudioTestMode::Normal || AUDIO_PERFORMANCE_DEBUG;

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
constexpr BaseType_t AUDIO_TASK_CORE = 0;

// ====================== DIGITAL AUDIO INPUT ======================

constexpr bool AUDIO_CAPTURE_ENABLED = true;
constexpr bool SOUND_DETECTOR_ENABLED =
    AUDIO_TEST_MODE == AudioTestMode::Normal ||
    AUDIO_TEST_MODE == AudioTestMode::SoundEvents || AUDIO_PERFORMANCE_DEBUG;
constexpr bool MUSIC_ANALYZER_ENABLED =
    AUDIO_TEST_MODE == AudioTestMode::Normal ||
    AUDIO_TEST_MODE == AudioTestMode::MusicEvents || AUDIO_PERFORMANCE_DEBUG;
constexpr bool VOICE_ENABLED =
    AUDIO_TEST_MODE == AudioTestMode::Normal ||
    AUDIO_TEST_MODE == AudioTestMode::VoiceEvents || AUDIO_PERFORMANCE_DEBUG;
constexpr bool VOICE_DEBUG =
    AUDIO_TEST_MODE == AudioTestMode::VoiceEvents;
// Edge Impulse's own debug mode prints the complete feature vector (3960
// floats) for every inference. Keep this off during normal operation: at
// 115200 baud that output blocks Serial and makes the whole robot stutter.
constexpr bool VOICE_EI_RAW_DEBUG = false;

// Prototype-only INMP441 wiring. GPIO10/11/12 overlap the preliminary product
// pin map and are not final assignments.
constexpr uint8_t INMP441_BCLK_PIN = 10;
constexpr uint8_t INMP441_WS_PIN = 11;
constexpr uint8_t INMP441_DATA_PIN = 12;
constexpr uint32_t AUDIO_INPUT_SAMPLE_RATE = 16000;
constexpr size_t AUDIO_CAPTURE_FRAME_SAMPLES = 256;
constexpr size_t AUDIO_CAPTURE_RING_SAMPLES = 32768;
constexpr uint8_t AUDIO_CAPTURE_QUEUE_FRAMES = 6;
constexpr uint32_t AUDIO_CAPTURE_TASK_STACK_WORDS = 4096;
constexpr UBaseType_t AUDIO_CAPTURE_TASK_PRIORITY = 3;
constexpr BaseType_t AUDIO_CAPTURE_TASK_CORE = 0;
constexpr uint32_t DATASET_SERIAL_BAUD = 921600;
constexpr size_t DATASET_RECORD_SAMPLES = AUDIO_INPUT_SAMPLE_RATE;

constexpr uint32_t VOICE_WINDOW_MS = 1000;
// Keep strongly overlapping windows: the short wake word must be present in
// two inference windows to satisfy VOICE_CONFIRM_WINDOWS. Gesture latency is
// handled in the APDS-9960 path instead of reducing wake-word coverage.
constexpr uint32_t VOICE_STEP_MS = 250;
// The wake word is short and often straddles two one-second windows. Requiring
// 0.85 in both windows made valid utterances easy to miss on the assembled
// robot. Two moderately strong windows are enough because waking only opens
// the command window; it does not execute an action by itself.
constexpr float VOICE_WAKE_THRESHOLD = 0.72F;
// A single exceptionally clear wake-word window may open command mode even if
// the neighbouring, differently aligned window contains only part of the word.
constexpr float VOICE_WAKE_SINGLE_WINDOW_THRESHOLD = 0.90F;
constexpr float VOICE_COMMAND_THRESHOLD = 0.80F;
// A very strong command result inside the explicit four-second command window
// can also be accepted from one window.
constexpr float VOICE_COMMAND_SINGLE_WINDOW_THRESHOLD = 0.90F;
// Event-only diagnostics for wake and command windows. Scores below this value
// remain silent; this does not affect recognition or confirmation thresholds.
constexpr float VOICE_DEBUG_NEAR_THRESHOLD = 0.50F;
constexpr uint8_t VOICE_CONFIRM_WINDOWS = 2;
// Positive windows for one utterance do not have to be adjacent. They must,
// however, belong to this bounded interval.
constexpr uint32_t VOICE_CONFIRM_SPAN_MS = 1200;
// After a recognition, require the class to disappear before it can fire again.
constexpr uint32_t VOICE_RELEASE_MS = 750;
constexpr uint32_t VOICE_RECOGNITION_COOLDOWN_MS = 600;
constexpr uint32_t VOICE_COMMAND_WINDOW_MS = 4000;
constexpr uint32_t VOICE_COMMAND_GUARD_MS = 300;
constexpr uint32_t VOICE_COMMAND_LOCKOUT_MS = 1500;
constexpr uint8_t VOICE_MAX_CONSECUTIVE_ERRORS = 5;
constexpr uint32_t VOICE_ERROR_PAUSE_MS = 5000;
// run_classifier() is slower than the 250 ms window step, so without a short
// Blocked interval the priority-1 voice task can starve its core's idle task.
constexpr uint32_t VOICE_POST_INFERENCE_YIELD_MS = 2;
// Reserve one reusable PSRAM block for the current and upcoming command model.
// Requests below the minimum keep their normal SDK allocation lifetime.
constexpr size_t VOICE_TENSOR_ARENA_MIN_REQUEST_BYTES = 128 * 1024;
constexpr size_t VOICE_TENSOR_ARENA_BYTES = 384 * 1024;
// ESP-IDF's xTaskCreate() stack depth is expressed in bytes. The standalone
// validated classifier also runs on the Arduino 8 KiB loop-task stack.
constexpr uint32_t VOICE_TASK_STACK_WORDS = 8192;
constexpr UBaseType_t VOICE_TASK_PRIORITY = 1;
constexpr BaseType_t VOICE_TASK_CORE = 1;

// The existing music FFT remains at 8 kHz. MusicAnalyzer applies an
// anti-aliasing half-band FIR before decimation by two.
constexpr uint32_t MUSIC_SAMPLE_RATE = 8000;
constexpr size_t MUSIC_FFT_SIZE = 256;
constexpr uint8_t MUSIC_BAND_COUNT = 8;
constexpr uint32_t MUSIC_TASK_STACK_WORDS = 4096;
// Music analysis is best-effort background work. Keep it at idle priority so
// FreeRTOS time slicing guarantees that IDLE0 can feed the CPU 0 task WDT even
// when audio frames or other system work keep the music task continuously Ready.
constexpr UBaseType_t MUSIC_TASK_PRIORITY = tskIDLE_PRIORITY;
constexpr BaseType_t MUSIC_TASK_CORE = 0;
// Also enter Blocked state between frames to shed queued work under overload.
constexpr uint32_t MUSIC_POST_FRAME_YIELD_MS = 2;

constexpr uint32_t SOUND_TASK_STACK_WORDS = 4096;
constexpr UBaseType_t SOUND_TASK_PRIORITY = 2;
constexpr BaseType_t SOUND_TASK_CORE = 0;

// Experimental PCM16 thresholds. These are safe initial values, not a claim
// of calibration; tune them from real INMP441 logs on the assembled robot.
constexpr float SOUND_MIN_RMS = 700.0F;
constexpr float SOUND_NOISE_MULTIPLIER = 2.8F;
constexpr float SOUND_NOISE_ATTACK = 0.002F;
constexpr float SOUND_NOISE_RELEASE = 0.08F;
constexpr uint8_t SOUND_HISTORY_FRAMES = 6;
constexpr uint8_t SOUND_REQUIRED_ACTIVE_FRAMES = 4;
// An ordinary sound is reported only after this much continuous silence. This
// keeps the Listening reaction behind the speaker instead of interrupting it.
constexpr unsigned long SOUND_END_SILENCE_MS = 375;
// After publishing SOUND or CLAP, require about 0.5 s below the activity gate
// before the detector can publish another event from a new sound episode.
constexpr uint8_t SOUND_RELEASE_FRAMES = 32;
constexpr unsigned long LISTENING_SOUND_COOLDOWN_MS = 2500;
constexpr int32_t CLAP_MIN_PEAK = 7000;
constexpr float CLAP_NOISE_MULTIPLIER = 6.0F;
constexpr int32_t CLAP_MIN_RISE = 1000;
// Acoustic claps in the physical test had peak-to-peak/RMS >= 5.57, while
// the remaining false table impacts were <= 4.34.
constexpr float CLAP_MIN_PEAK_TO_RMS_RATIO = 5.0F;
// Mechanical impacts in the test logs displaced the complete PCM frame by
// 1900..7400 counts, while acoustic claps stayed within about 1200 counts of
// the quiet DC baseline. Validate the whole impulse, not only its first frame.
constexpr float CLAP_MAX_DC_EXCURSION = 1800.0F;
constexpr float SOUND_DC_BASELINE_RATE = 0.02F;
constexpr float CLAP_DECAY_RATIO = 0.40F;
constexpr uint32_t CLAP_MAX_DURATION_MS = 180;
constexpr uint32_t AUDIO_EVENT_COOLDOWN_MS = 400;
constexpr uint32_t MIC_SELF_AUDIO_GUARD_MS = 250;
constexpr bool MIC_DEBUG =
    AUDIO_TEST_MODE == AudioTestMode::SoundEvents;

// Music detector tuning. Energy is measured after DC removal and Hann
// windowing; all thresholds live here so field tuning does not change code.
constexpr float MUSIC_MIN_ENERGY = 1200.0F;
// AC-RMS is measured before FFT. Physical tests showed about 30..100 in
// silence and more than 700 for medium-volume music.
constexpr float MUSIC_MIN_SIGNAL_RMS = 200.0F;
// A regular music bed is not silence: use a modest energy gate and let the
// onset/interval checks reject speech and steady noise.
constexpr float MUSIC_NOISE_MULTIPLIER = 1.35F;
constexpr float MUSIC_ONSET_RATIO = 0.09F;
constexpr unsigned long MUSIC_BEAT_REFRACTORY_MS = 180;
constexpr uint16_t MUSIC_MIN_BPM = 60;
constexpr uint16_t MUSIC_MAX_BPM = 200;
constexpr uint8_t MUSIC_REQUIRED_BEATS = 6;
constexpr float MUSIC_INTERVAL_TOLERANCE = 0.28F;
// Require three matching regular windows: physical speech tests produced two
// qualifying windows in a row. Non-qualifying beats between these windows do
// not erase progress, provided all three arrive within the deadline below.
// Once active, PCM RMS below the signal gate for the stop timeout ends MUSIC.
constexpr float MUSIC_START_CONFIDENCE = 0.65F;
// Start confirmation needs sustained foreground audio. In physical logs the
// third window of false speech starts was 284..327 RMS, while music candidates
// were normally above 600 RMS.
constexpr float MUSIC_START_MIN_SIGNAL_RMS = 600.0F;
constexpr uint8_t MUSIC_START_REGULAR_WINDOWS = 3;
// Matching windows must cover real time as well as count. This prevents three
// overlapping speech windows from starting MUSIC immediately.
constexpr unsigned long MUSIC_START_MIN_SPAN_MS = 900;
constexpr unsigned long MUSIC_START_CONFIRM_MS = 4000;
constexpr float MUSIC_START_BPM_TOLERANCE = 0.10F;
// Reject a cluster of speech syllables that happens to look rhythmical. Six
// accepted onsets must cover a sustained passage. The lower bound also admits
// double-tempo onsets (for example hi-hats at 234 BPM folded to 117 BPM).
constexpr unsigned long MUSIC_MIN_CONFIRM_SPAN_MS = 1100;
constexpr unsigned long MUSIC_STOP_TIMEOUT_MS = 2500;
// Speech and room noise can remain above MUSIC_MIN_SIGNAL_RMS after playback
// stops. Do not let that broadband activity hold MUSIC forever: an active
// state must periodically receive another rhythm window of start quality.
constexpr unsigned long MUSIC_RHYTHM_STOP_TIMEOUT_MS = 4500;
constexpr float MUSIC_SPECTRUM_ATTACK = 0.65F;
constexpr float MUSIC_SPECTRUM_RELEASE = 0.18F;
constexpr bool MUSIC_DEBUG =
    AUDIO_TEST_MODE == AudioTestMode::MusicEvents;

// Character animation used while the confirmed MUSIC state is visible.
constexpr float MUSIC_EYE_SWAY_DEGREES = 5.0F;
constexpr unsigned long MUSIC_EYE_SWAY_CYCLE_MS = 1200;
constexpr unsigned long MUSIC_EYE_SWAY_FADE_MS = 300;
constexpr unsigned long MUSIC_SQUINT_MIN_INTERVAL_MS = 3500;
constexpr unsigned long MUSIC_SQUINT_MAX_INTERVAL_MS = 6500;
constexpr unsigned long MUSIC_SQUINT_CLOSE_MS = 180;
constexpr unsigned long MUSIC_SQUINT_MIN_HOLD_MS = 400;
constexpr unsigned long MUSIC_SQUINT_MAX_HOLD_MS = 700;
constexpr unsigned long MUSIC_SQUINT_OPEN_MS = 220;
constexpr float MUSIC_SQUINT_HEIGHT_RATIO = 0.40F;
constexpr unsigned long MUSIC_NOTE_MIN_INTERVAL_MS = 1800;
constexpr unsigned long MUSIC_NOTE_MAX_INTERVAL_MS = 3200;
constexpr unsigned long MUSIC_NOTE_MIN_LIFETIME_MS = 900;
constexpr unsigned long MUSIC_NOTE_MAX_LIFETIME_MS = 1400;
constexpr uint8_t MUSIC_NOTE_MIN_RISE_PX = 6;
constexpr uint8_t MUSIC_NOTE_MAX_RISE_PX = 12;
constexpr uint8_t MUSIC_NOTE_MAX_COUNT = 2;
constexpr unsigned long MUSIC_FIRST_NOTE_DELAY_MS = 700;
constexpr unsigned long MUSIC_FIRST_SQUINT_DELAY_MS = 500;


// ===================== ПОРОГИ СОСТОЯНИЙ =====================

// Расстояние в миллиметрах, не дальше которого включается состояние Happy.
constexpr uint16_t HAPPY_DISTANCE_MM = 600;

// Расстояние в миллиметрах, начиная с которого Happy сменяется на Curious.
constexpr uint16_t CURIOUS_DISTANCE_MM = 700;

// The assembled enclosure produces a stable internal reflection around
// 80..120 mm even with an empty scene. Measurements below this boundary are
// therefore not usable as presence observations.
constexpr uint16_t DISTANCE_MIN_VALID_MM = 150;

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

// Automatic sleep requires both conditions to remain stable. Manual sleep is
// deliberately independent from these delays and from the light sensor.
constexpr unsigned long AUTO_SLEEP_DARK_DELAY_MS = 30000;
constexpr unsigned long AUTO_SLEEP_ABSENCE_DELAY_MS = 30000;

// ======================== ИНТЕРВАЛЫ ========================

// Период между измерениями расстояния VL53L0X в миллисекундах.
constexpr unsigned long SENSOR_INTERVAL_MS = 100;

// Период диагностического вывода расстояния и RangeStatus.
constexpr unsigned long DISTANCE_LOG_INTERVAL_MS = 500;

// Период между измерениями освещённости BH1750 в миллисекундах.
constexpr unsigned long LIGHT_INTERVAL_MS = 500;

// A full 128x64 framebuffer occupies the shared I2C bus for about 23 ms at
// 400 kHz. 15 FPS is still smooth on the SSD1306, but leaves substantially
// more bus time for APDS-9960 FIFO reads and the ranging sensors.
constexpr unsigned long DISPLAY_INTERVAL_MS = 66;

// Период чтения акселерометра и гироскопа LSM6DS3 в миллисекундах.
constexpr unsigned long IMU_INTERVAL_MS = 20;

// Poll faster than the display cadence so the APDS-9960 FIFO is drained before
// a full OLED transfer. Reads remain bounded to one FIFO batch per poll.
constexpr unsigned long GESTURE_INTERVAL_MS = 10;
constexpr uint8_t GESTURE_MIN_SAMPLE_VALUE = 10;
constexpr uint8_t GESTURE_MIN_SAMPLES = 4;
constexpr int16_t GESTURE_DIRECTION_THRESHOLD = 28;
constexpr float GESTURE_DOMINANT_AXIS_RATIO = 1.15F;
constexpr unsigned long GESTURE_END_TIMEOUT_MS = 90;
constexpr unsigned long GESTURE_COOLDOWN_MS = 350;

// ====================== WI-FI / SYSTEM TIME ======================

// POSIX TZ syntax uses the opposite sign: MSK-3 means UTC+3, no DST.
constexpr char TIMEZONE[] = "MSK-3";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.google.com";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr unsigned long TIME_SYNC_TIMEOUT_MS = 30000;
constexpr unsigned long TIME_RETRY_INTERVAL_MS = 300000;

// ====================== SMART LIGHTING / TUYA ======================

constexpr char TUYA_RELAY_DPS[] = "1";
constexpr uint32_t TUYA_OPERATION_TIMEOUT_MS = 2500;
constexpr unsigned long TUYA_ERROR_BACKOFF_MS = 5000;
constexpr unsigned long TUYA_STATUS_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long TUYA_STATUS_RETRY_MS = 60UL * 1000UL;
constexpr uint8_t TUYA_REQUEST_QUEUE_DEPTH = 4;
constexpr uint8_t TUYA_RESULT_QUEUE_DEPTH = 4;
constexpr uint32_t TUYA_TASK_STACK_BYTES = 8192;
constexpr UBaseType_t TUYA_TASK_PRIORITY = 1;
constexpr BaseType_t TUYA_TASK_CORE = 1;
constexpr time_t TUYA_MINIMUM_VALID_TIME = 1577836800;  // 2020-01-01 UTC

// Light measurements are filtered first, then compared as two robust plateaus:
// the median of the recent 3 seconds against the median of the preceding
// 20 seconds. The 10% drop threshold sits between the latest measured
// in-state range (about 7.3%) and the main-light transition (13.7%). The
// noise multiplier makes the effective threshold rise
// automatically when the sensor signal is unstable.
constexpr float LIGHT_MAX_VALID_LUX = 120000.0F;
constexpr float LIGHT_FILTER_ALPHA = 0.25F;
constexpr float LIGHT_RELATIVE_DROP_RATIO = 0.10F;
constexpr float LIGHT_RELATIVE_MIN_BASELINE_LUX = 25.0F;
constexpr float LIGHT_NOISE_MULTIPLIER = 5.0F;
constexpr float LIGHT_EVENING_MAX_LUX = 12.0F;
constexpr uint8_t LIGHT_DAY_START_HOUR = 8;
constexpr uint8_t LIGHT_EVENING_START_HOUR = 17;
constexpr uint8_t LIGHT_EVENING_END_HOUR = 1;
constexpr unsigned long LIGHT_CONFIRMATION_MS = 5000;
constexpr unsigned long LIGHT_MANUAL_OVERRIDE_MS = 2UL * 60UL * 60UL * 1000UL;
constexpr unsigned long LIGHT_AUTOMATION_RETRY_MS = 60000;
constexpr unsigned long LIGHT_DIAGNOSTIC_INTERVAL_MS = 1000;
constexpr bool LIGHT_AUTOMATION_ENABLED_BY_DEFAULT = true;

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

// Duration of the dedicated startled reaction to a microphone clap.
constexpr unsigned long STARTLED_DURATION_MS = 1100;

// Duration of the temporary visual reaction to sustained external sound.
constexpr unsigned long LISTENING_DURATION_MS = 1500;

constexpr unsigned long GREETING_DURATION_MS = 1400;

// Voice-command reactions and temporary information screens.
constexpr unsigned long VOICE_DANCE_DURATION_MS = 10000;
constexpr unsigned long VOICE_INFO_SCREEN_DURATION_MS = 10000;
constexpr unsigned long VOICE_WEATHER_TOTAL_TIMEOUT_MS = 15000;
constexpr unsigned long VOICE_TIMEOUT_REACTION_MS = 1200;

// Пауза после постановки, за которую ToF и корпус успевают стабилизироваться.
constexpr unsigned long PUT_DOWN_SETTLE_MS = 500;

// Продолжительность головокружения после встряхивания, в миллисекундах.
constexpr unsigned long DIZZY_DURATION_MS = 1500;

}  // пространство имён config
