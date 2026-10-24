#pragma once

#include <stddef.h>
#include <stdint.h>

namespace edge_impulse_voice {

// Fixed capacity keeps inference deterministic and prevents Edge Impulse
// result types from leaking into the application layer.
constexpr size_t RESULT_CAPACITY = 16;

struct Score {
  const char* label = nullptr;
  float value = 0.0F;
};

struct Result {
  Score scores[RESULT_CAPACITY] = {};
  size_t scoreCount = 0;
  uint32_t dspMs = 0;
  uint32_t classificationMs = 0;
  int errorCode = 0;
  bool success = false;
};

size_t sampleCount();
uint32_t sampleRate();
void begin();

// The model is synchronous and intentionally non-reentrant. DeskBot calls it
// only from the single voice task.
Result infer(const int16_t* samples, size_t count, bool rawDebug);

}  // namespace edge_impulse_voice
