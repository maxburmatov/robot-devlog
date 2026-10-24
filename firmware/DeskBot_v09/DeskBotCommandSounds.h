#pragma once

#include <Arduino.h>

// Original, locally synthesized DeskBot UI cues. PCM16 mono, 22050 Hz. The
// repeated rounded wave cycles keep the source compact while still producing
// complete embedded PCM arrays (no run-time oscillator or blocking delay).
constexpr uint32_t DESKBOT_COMMAND_SOUND_SAMPLE_RATE = 22050;

#define DB_REPEAT_2(...) __VA_ARGS__ __VA_ARGS__
#define DB_REPEAT_4(...) DB_REPEAT_2(__VA_ARGS__) DB_REPEAT_2(__VA_ARGS__)
#define DB_REPEAT_8(...) DB_REPEAT_4(__VA_ARGS__) DB_REPEAT_4(__VA_ARGS__)
#define DB_REPEAT_16(...) DB_REPEAT_8(__VA_ARGS__) DB_REPEAT_8(__VA_ARGS__)
#define DB_REPEAT_32(...) DB_REPEAT_16(__VA_ARGS__) DB_REPEAT_16(__VA_ARGS__)
#define DB_REPEAT_64(...) DB_REPEAT_32(__VA_ARGS__) DB_REPEAT_32(__VA_ARGS__)
#define DB_REPEAT_128(...) DB_REPEAT_64(__VA_ARGS__) DB_REPEAT_64(__VA_ARGS__)
#define DB_REPEAT_256(...) DB_REPEAT_128(__VA_ARGS__) DB_REPEAT_128(__VA_ARGS__)

#define DB_WARM_LOW \
  0, 210, 420, 620, 800, 950, 1060, 1120, 1130, 1070, 960, 800, 610, 400, 190, 0, \
  -170, -320, -450, -540, -590, -600, -560, -480, -370, -250, -120, 0, 110, 180, 120, 0,
#define DB_WARM_FULL \
  0, 1450, 2860, 4140, 5200, 5960, 6370, 6420, 6130, 5560, 4810, 3940, 3020, 2100, 1240, 550, \
  0, -430, -790, -1100, -1380, -1610, -1760, -1780, -1640, -1360, -990, -600, -250, 0, 120, 90,
const int16_t SOUND_COMMAND_HELLO[] PROGMEM = {
    DB_REPEAT_8(DB_WARM_LOW)
    DB_REPEAT_256(DB_WARM_FULL)
    DB_REPEAT_4(DB_WARM_FULL)
    DB_REPEAT_8(DB_WARM_LOW)
};
constexpr size_t SOUND_COMMAND_HELLO_SAMPLES =
    sizeof(SOUND_COMMAND_HELLO) / sizeof(SOUND_COMMAND_HELLO[0]);

#define DB_DONE_LOW \
  0, 260, 500, 690, 810, 850, 800, 680, 510, 310, 110, -60, -190, -260, \
  -250, -180, -80, 20, 90, 110, 80, 20, -30, -50, -30, 0, 10, 0,
#define DB_DONE_FULL \
  0, 1700, 3260, 4490, 5230, 5370, 4890, 3910, 2660, 1350, 210, -590, -990, -1020, \
  -790, -440, -100, 120, 190, 100, -40, -120, -100, -20, 40, 50, 20, 0,
const int16_t SOUND_COMMAND_DONE[] PROGMEM = {
    DB_REPEAT_4(DB_DONE_LOW)
    DB_REPEAT_64(DB_DONE_FULL)
    DB_REPEAT_32(DB_DONE_FULL)
    DB_REPEAT_16(DB_DONE_FULL)
    DB_REPEAT_8(DB_DONE_FULL)
    DB_REPEAT_8(DB_DONE_LOW)
};
constexpr size_t SOUND_COMMAND_DONE_SAMPLES =
    sizeof(SOUND_COMMAND_DONE) / sizeof(SOUND_COMMAND_DONE[0]);

#define DB_LISTEN_LOW \
  0, 110, 220, 320, 400, 460, 490, 490, 460, 400, 320, 230, 130, 40, -30, -80, \
  -100, -90, -60, -20, 20, 40, 40, 20, 0, -10, 0, 10, 10, 0, 0, 0,
#define DB_LISTEN_FULL \
  0, 760, 1450, 1990, 2310, 2380, 2210, 1830, 1320, 770, 270, -100, -310, -380, -330, -220, \
  -100, 0, 50, 40, 0, -30, -30, 0, 20, 20, 0, -10, 0, 0, 0, 0,
const int16_t SOUND_LISTENING_CUE[] PROGMEM = {
    DB_REPEAT_8(DB_LISTEN_LOW)
    DB_REPEAT_128(DB_LISTEN_FULL)
    DB_REPEAT_16(DB_LISTEN_FULL)
    DB_REPEAT_8(DB_LISTEN_FULL)
    DB_REPEAT_4(DB_LISTEN_FULL)
    DB_REPEAT_8(DB_LISTEN_LOW)
    DB_LISTEN_LOW
};
constexpr size_t SOUND_LISTENING_CUE_SAMPLES =
    sizeof(SOUND_LISTENING_CUE) / sizeof(SOUND_LISTENING_CUE[0]);

#undef DB_LISTEN_FULL
#undef DB_LISTEN_LOW
#undef DB_DONE_FULL
#undef DB_DONE_LOW
#undef DB_WARM_FULL
#undef DB_WARM_LOW
#undef DB_REPEAT_256
#undef DB_REPEAT_128
#undef DB_REPEAT_64
#undef DB_REPEAT_32
#undef DB_REPEAT_16
#undef DB_REPEAT_8
#undef DB_REPEAT_4
#undef DB_REPEAT_2
