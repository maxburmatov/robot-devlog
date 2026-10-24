#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

enum class TuyaConnectionState : uint8_t {
  Disabled,
  Disconnected,
  Connecting,
  Ready,
  Error,
};

enum class TuyaRequestKind : uint8_t {
  Status,
  TurnOn,
  TurnOff,
};

struct TuyaResult {
  TuyaRequestKind request = TuyaRequestKind::Status;
  bool success = false;
  bool relayKnown = false;
  bool relayOn = false;
  unsigned long completedAt = 0;
  char error[40] = {};
};

class TuyaPlugClient {
 public:
  bool begin();
  void update(unsigned long now);
  bool requestStatus();
  bool requestRelay(bool on);
  bool takeResult(TuyaResult& result);

  bool busy() const;
  bool configured() const;
  bool relayKnown() const;
  bool relayOn() const;
  TuyaConnectionState state() const;
  unsigned long lastSuccessAt() const;
  const char* lastError() const;
  static const char* stateName(TuyaConnectionState state);

 private:
  static void taskEntry(void* parameter);
  void runTask();
  bool enqueue(TuyaRequestKind request);

  QueueHandle_t requestQueue_ = nullptr;
  QueueHandle_t resultQueue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  TuyaConnectionState state_ = TuyaConnectionState::Disabled;
  bool configured_ = false;
  bool busy_ = false;
  bool relayKnown_ = false;
  bool relayOn_ = false;
  unsigned long lastSuccessAt_ = 0;
  unsigned long nextRequestAt_ = 0;
  char lastError_[40] = {};
  TuyaResult pendingResult_;
  bool pendingResultAvailable_ = false;
};
