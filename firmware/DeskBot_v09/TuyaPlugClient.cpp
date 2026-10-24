#include "TuyaPlugClient.h"

#include <LynkTuya.h>
#include <WiFi.h>
#include <cstring>
#include <ctime>

#include "Config.h"
#include "TuyaSecrets.h"

namespace {

bool keyConfigured() {
  return std::strlen(TUYA_LOCAL_KEY) == 16 &&
         std::strcmp(TUYA_LOCAL_KEY, "0123456789abcdef") != 0;
}

}  // namespace

bool TuyaPlugClient::begin() {
  configured_ = keyConfigured();
  if (!configured_) {
    state_ = TuyaConnectionState::Disabled;
    strncpy(lastError_, "LOCAL_KEY_NOT_CONFIGURED", sizeof(lastError_) - 1);
    Serial.println("[TUYA] disabled: configure 16-byte TUYA_LOCAL_KEY");
    return false;
  }

  requestQueue_ = xQueueCreate(config::TUYA_REQUEST_QUEUE_DEPTH,
                               sizeof(TuyaRequestKind));
  resultQueue_ = xQueueCreate(config::TUYA_RESULT_QUEUE_DEPTH,
                              sizeof(TuyaResult));
  if (requestQueue_ == nullptr || resultQueue_ == nullptr) {
    state_ = TuyaConnectionState::Error;
    strncpy(lastError_, "QUEUE_ALLOCATION_FAILED", sizeof(lastError_) - 1);
    return false;
  }
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "tuya", config::TUYA_TASK_STACK_BYTES, this,
      config::TUYA_TASK_PRIORITY, &task_, config::TUYA_TASK_CORE);
  if (created != pdPASS) {
    state_ = TuyaConnectionState::Error;
    strncpy(lastError_, "TASK_CREATE_FAILED", sizeof(lastError_) - 1);
    return false;
  }
  state_ = TuyaConnectionState::Disconnected;
  Serial.printf("[TUYA] ready ip=%u.%u.%u.%u protocol=3.5 dps=%s\n",
                TUYA_PLUG_IP[0], TUYA_PLUG_IP[1], TUYA_PLUG_IP[2],
                TUYA_PLUG_IP[3],
                config::TUYA_RELAY_DPS);
  return true;
}

void TuyaPlugClient::update(unsigned long now) {
  TuyaResult result;
  while (resultQueue_ != nullptr &&
         xQueueReceive(resultQueue_, &result, 0) == pdTRUE) {
    busy_ = false;
    relayKnown_ = result.relayKnown;
    relayOn_ = result.relayOn;
    if (result.success) {
      state_ = TuyaConnectionState::Ready;
      lastSuccessAt_ = now;
      nextRequestAt_ = 0;
      lastError_[0] = '\0';
    } else {
      state_ = TuyaConnectionState::Error;
      nextRequestAt_ = now + config::TUYA_ERROR_BACKOFF_MS;
      strncpy(lastError_, result.error, sizeof(lastError_) - 1);
      lastError_[sizeof(lastError_) - 1] = '\0';
    }
    pendingResult_ = result;
    pendingResultAvailable_ = true;
  }
}

bool TuyaPlugClient::requestStatus() {
  return enqueue(TuyaRequestKind::Status);
}

bool TuyaPlugClient::requestRelay(bool on) {
  return enqueue(on ? TuyaRequestKind::TurnOn : TuyaRequestKind::TurnOff);
}

bool TuyaPlugClient::takeResult(TuyaResult& result) {
  if (!pendingResultAvailable_) return false;
  result = pendingResult_;
  pendingResultAvailable_ = false;
  return true;
}

bool TuyaPlugClient::enqueue(TuyaRequestKind request) {
  const unsigned long now = millis();
  if (!configured_ || requestQueue_ == nullptr || busy_) return false;
  if (nextRequestAt_ != 0 && static_cast<int32_t>(nextRequestAt_ - now) > 0) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    state_ = TuyaConnectionState::Disconnected;
    strncpy(lastError_, "WIFI_DISCONNECTED", sizeof(lastError_) - 1);
    return false;
  }
  if (xQueueSend(requestQueue_, &request, 0) != pdTRUE) return false;
  busy_ = true;
  state_ = TuyaConnectionState::Connecting;
  return true;
}

bool TuyaPlugClient::busy() const { return busy_; }
bool TuyaPlugClient::configured() const { return configured_; }
bool TuyaPlugClient::relayKnown() const { return relayKnown_; }
bool TuyaPlugClient::relayOn() const { return relayOn_; }
TuyaConnectionState TuyaPlugClient::state() const { return state_; }
unsigned long TuyaPlugClient::lastSuccessAt() const { return lastSuccessAt_; }
const char* TuyaPlugClient::lastError() const { return lastError_; }

void TuyaPlugClient::taskEntry(void* parameter) {
  static_cast<TuyaPlugClient*>(parameter)->runTask();
}

void TuyaPlugClient::runTask() {
  const IPAddress ip(TUYA_PLUG_IP[0], TUYA_PLUG_IP[1], TUYA_PLUG_IP[2],
                     TUYA_PLUG_IP[3]);
  LynkTuyaDevice<TUYA_V35> device(ip, TUYA_LOCAL_KEY);
  device.setTimeout(config::TUYA_OPERATION_TIMEOUT_MS);

  for (;;) {
    TuyaRequestKind request;
    if (xQueueReceive(requestQueue_, &request, portMAX_DELAY) != pdTRUE) continue;

    TuyaResult result;
    result.request = request;
    bool relay = false;
    if (WiFi.status() != WL_CONNECTED) {
      strncpy(result.error, "WIFI_DISCONNECTED", sizeof(result.error) - 1);
    } else if (request == TuyaRequestKind::Status) {
      result.success = device.getRelayState(relay, config::TUYA_RELAY_DPS, true);
      result.relayKnown = result.success;
      result.relayOn = relay;
      if (!result.success) {
        strncpy(result.error, "STATUS_FAILED", sizeof(result.error) - 1);
      }
    } else {
      const time_t timestamp = time(nullptr);
      if (timestamp < config::TUYA_MINIMUM_VALID_TIME) {
        strncpy(result.error, "TIME_NOT_SYNCHRONIZED", sizeof(result.error) - 1);
      } else {
        const bool desired = request == TuyaRequestKind::TurnOn;
        const bool sent = desired
                              ? device.turnOn(static_cast<uint32_t>(timestamp),
                                              false, config::TUYA_RELAY_DPS)
                              : device.turnOff(static_cast<uint32_t>(timestamp),
                                               false, config::TUYA_RELAY_DPS);
        const bool read = sent &&
            device.getRelayState(relay, config::TUYA_RELAY_DPS, true);
        result.relayKnown = read;
        result.relayOn = relay;
        result.success = sent && read && relay == desired;
        if (!sent) {
          device.closeConnection();
          strncpy(result.error, "COMMAND_FAILED", sizeof(result.error) - 1);
        } else if (!read) {
          strncpy(result.error, "VERIFY_FAILED", sizeof(result.error) - 1);
        } else if (relay != desired) {
          strncpy(result.error, "STATE_MISMATCH", sizeof(result.error) - 1);
        }
      }
    }
    result.completedAt = millis();
    xQueueSend(resultQueue_, &result, portMAX_DELAY);
  }
}

const char* TuyaPlugClient::stateName(TuyaConnectionState state) {
  switch (state) {
    case TuyaConnectionState::Disabled: return "DISABLED";
    case TuyaConnectionState::Disconnected: return "DISCONNECTED";
    case TuyaConnectionState::Connecting: return "CONNECTING";
    case TuyaConnectionState::Ready: return "READY";
    case TuyaConnectionState::Error: return "ERROR";
  }
  return "UNKNOWN";
}
