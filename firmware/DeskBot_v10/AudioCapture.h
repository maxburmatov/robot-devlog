#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

#include "Config.h"

struct AudioFrame {
  int16_t samples[config::AUDIO_CAPTURE_FRAME_SAMPLES];
  uint32_t sequence = 0;
  uint64_t endSample = 0;
  int64_t endedAtUs = 0;
  unsigned long endedAt = 0;
};

struct AudioCaptureStats {
  uint32_t capturedFrames = 0;
  uint64_t capturedSamples = 0;
  uint32_t soundDroppedFrames = 0;
  uint32_t musicDroppedFrames = 0;
  uint32_t readErrors = 0;
  uint32_t partialReads = 0;
  uint32_t soundQueueHighWater = 0;
  uint32_t musicQueueHighWater = 0;
  int64_t firstFrameEndedAtUs = 0;
  int64_t lastFrameEndedAtUs = 0;
  uint32_t minFramePeriodUs = UINT32_MAX;
  uint32_t maxFramePeriodUs = 0;
  uint64_t busyTimeUs = 0;
  uint32_t maxFrameWorkUs = 0;
  BaseType_t taskCore = tskNO_AFFINITY;
  UBaseType_t stackHighWaterMark = 0;
};

class AudioCapture {
 public:
  bool begin();
  bool ready() const;

  bool receiveSoundFrame(AudioFrame& frame, TickType_t waitTicks = 0);
  bool receiveMusicFrame(AudioFrame& frame, TickType_t waitTicks = 0);

  // Copies the newest stable window only when at least stepSamples of new
  // audio have arrived. The writer never waits for inference.
  bool copyLatestWindow(int16_t* destination, size_t sampleCount,
                        uint64_t& previousEndSample, size_t stepSamples);
  // Copies an exact historical interval [endSample - sampleCount, endSample).
  // The requested interval must still be present in the capture ring.
  bool copyWindowEndingAt(int16_t* destination, size_t sampleCount,
                          uint64_t endSample);

  AudioCaptureStats stats() const;
  uint64_t totalSamples() const;

 private:
  static void taskEntry(void* context);
  void task();
  void publishFrame(const AudioFrame& frame);
  bool sendNewest(QueueHandle_t queue, const AudioFrame& frame,
                  uint32_t& dropCounter, uint32_t& queueHighWater);

  QueueHandle_t soundQueue_ = nullptr;
  QueueHandle_t musicQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  int16_t* ring_ = nullptr;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  uint64_t totalSamples_ = 0;
  AudioCaptureStats stats_;
  bool ready_ = false;
};
