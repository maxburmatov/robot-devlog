#pragma once

#include <Arduino.h>

#include "AudioCapture.h"

enum class VoiceCommand : uint8_t {
  None,
  WakeWord,
  Hello,
  Dance,
  Time,
  Weather,
  Sleep,
  WakeUp,
};

struct VoiceRecognition {
  VoiceCommand command = VoiceCommand::None;
  float confidence = 0.0F;
};

struct VoiceDetectorStats {
  uint32_t droppedWindows = 0;
  uint32_t inferenceCount = 0;
  uint64_t busyTimeUs = 0;
  uint32_t maxInferenceUs = 0;
  uint32_t lastDspMs = 0;
  uint32_t lastNnMs = 0;
  BaseType_t taskCore = tskNO_AFFINITY;
  UBaseType_t stackHighWaterMark = 0;
};

const char* voiceCommandName(VoiceCommand command);

class VoiceCommandDetector {
 public:
  bool begin(AudioCapture& capture);
  VoiceRecognition takeRecognition();
  void setCommandMode(bool enabled);
  void setSuppressed(bool suppressed);
  bool ready() const;
  uint32_t droppedWindows() const;
  UBaseType_t stackHighWaterMark() const;
  VoiceDetectorStats stats() const;

 private:
  static void taskEntry(void* context);
  static int signalGetData(size_t offset, size_t length, float* output);
  void task();
  void resetConfirmation(bool resetWindow = false);

  static VoiceCommandDetector* callbackInstance_;
  AudioCapture* capture_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  int16_t* window_ = nullptr;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  VoiceRecognition pendingRecognition_;
  bool suppressed_ = false;
  bool commandMode_ = false;
  bool resetRequested_ = false;
  bool requireCleanWindow_ = false;
  bool ready_ = false;
  VoiceCommand confirmationCommand_ = VoiceCommand::None;
  uint8_t confirmedWindows_ = 0;
  unsigned long confirmationStartedAt_ = 0;
  VoiceCommand latchedCommand_ = VoiceCommand::None;
  unsigned long releaseStartedAt_ = 0;
  unsigned long cooldownUntil_ = 0;
  uint64_t previousEndSample_ = 0;
  uint32_t droppedWindows_ = 0;
  UBaseType_t stackHighWaterMark_ = 0;
  VoiceDetectorStats stats_;
};
