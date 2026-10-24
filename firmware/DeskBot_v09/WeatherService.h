#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

struct WeatherData {
  float temperature = 0.0F;
  float apparentTemperature = 0.0F;
  float windSpeed = 0.0F;
  uint8_t humidity = 0;
  uint8_t weatherCode = 0;
  bool isDay = true;
  bool valid = false;
  unsigned long receivedAt = 0;
};

enum class WeatherStatus {
  NoData,
  Loading,
  Ready,
  Stale,
  Error,
};

// Schedules weather downloads from loop(), while all blocking TLS/HTTP and
// parsing work happens in a dedicated FreeRTOS task.
class WeatherService {
 public:
  void begin(unsigned long now);
  void update(unsigned long now);
  void requestRefresh(unsigned long now);
  bool getWeather(WeatherData& data) const;
  bool isLoading() const;
  bool hasError() const;
  WeatherStatus status() const;
  uint32_t revision() const;

 private:
  struct RequestResult {
    WeatherData data;
    int httpCode = 0;
    bool success = false;
    bool parseError = false;
    bool usedHttpFallback = false;
    char errorDetail[192] = {};
  };

  static void requestTask(void* parameter);
  static bool deadlineReached(unsigned long now, unsigned long deadline);
  void startRequest(unsigned long now);
  void finishRequest(const RequestResult& result, unsigned long now);
  void setStatus(WeatherStatus status);

  QueueHandle_t resultQueue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  WeatherData data_;
  WeatherStatus status_ = WeatherStatus::NoData;
  unsigned long nextRequestAt_ = 0;
  uint32_t revision_ = 0;
  bool started_ = false;
  bool waitingForWifiLogged_ = false;
  bool wifiWasConnected_ = false;
  unsigned long wifiReadyAt_ = 0;
};
