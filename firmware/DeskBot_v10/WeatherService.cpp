#include "WeatherService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "Config.h"
#include "Secrets.h"

namespace {

constexpr char WEATHER_HOST[] = "wttr.in";
constexpr uint16_t WEATHER_HTTPS_PORT = 443;
constexpr uint16_t WEATHER_HTTP_PORT = 80;

bool readFirstNumber(const String& text, float& value) {
  const char* start = text.c_str();
  while (*start != '\0' && *start != '+' && *start != '-' &&
         (*start < '0' || *start > '9')) {
    ++start;
  }
  if (*start == '\0') return false;

  char* end = nullptr;
  value = strtof(start, &end);
  return end != start && std::isfinite(value);
}

uint8_t weatherCodeFor(String condition) {
  condition.toLowerCase();
  if (condition.indexOf("thunder") >= 0) return 95;
  if (condition.indexOf("snow") >= 0 ||
      condition.indexOf("blizzard") >= 0 ||
      condition.indexOf("sleet") >= 0 || condition.indexOf("ice") >= 0) {
    return 71;
  }
  if (condition.indexOf("rain") >= 0 ||
      condition.indexOf("drizzle") >= 0 ||
      condition.indexOf("shower") >= 0) {
    return 61;
  }
  if (condition.indexOf("fog") >= 0 || condition.indexOf("mist") >= 0) {
    return 45;
  }
  if (condition.indexOf("overcast") >= 0) return 3;
  if (condition.indexOf("cloud") >= 0) return 2;
  return 0;
}

bool parseResponse(const String& response, WeatherData& data) {
  const int separator1 = response.indexOf('|');
  const int separator2 = response.indexOf('|', separator1 + 1);
  const int separator3 = response.indexOf('|', separator2 + 1);
  const int separator4 = response.indexOf('|', separator3 + 1);
  if (separator1 < 0 || separator2 < 0 || separator3 < 0 ||
      separator4 < 0) {
    return false;
  }

  float temperature = 0.0F;
  float apparentTemperature = 0.0F;
  float humidity = 0.0F;
  float windSpeed = 0.0F;
  if (!readFirstNumber(response.substring(0, separator1), temperature) ||
      !readFirstNumber(response.substring(separator1 + 1, separator2),
                       apparentTemperature) ||
      !readFirstNumber(response.substring(separator2 + 1, separator3),
                       humidity) ||
      !readFirstNumber(response.substring(separator3 + 1, separator4),
                       windSpeed)) {
    return false;
  }
  if (humidity < 0.0F || humidity > 100.0F || windSpeed < 0.0F) {
    return false;
  }

  String condition = response.substring(separator4 + 1);
  condition.trim();
  tm localTime = {};
  const time_t now = time(nullptr);
  const bool timeIsValid = now >= 1577836800 &&
                           localtime_r(&now, &localTime) != nullptr;

  data.temperature = temperature;
  data.apparentTemperature = apparentTemperature;
  data.humidity = static_cast<uint8_t>(lroundf(humidity));
  data.weatherCode = weatherCodeFor(condition);
  data.windSpeed = windSpeed / 3.6F;  // wttr.in returns km/h; UI uses m/s.
  data.isDay = !timeIsValid || (localTime.tm_hour >= 6 && localTime.tm_hour < 21);
  data.valid = true;
  data.receivedAt = millis();
  return true;
}

}  // namespace

void WeatherService::begin(unsigned long now) {
  if (started_) return;
  resultQueue_ = xQueueCreate(1, sizeof(RequestResult));
  started_ = resultQueue_ != nullptr;
  nextRequestAt_ = now;
  if (!started_) {
    setStatus(WeatherStatus::Error);
    Serial.println("WEATHER: task queue allocation failed");
  }
}

void WeatherService::update(unsigned long now) {
  if (!started_) return;

  RequestResult result;
  if (xQueueReceive(resultQueue_, &result, 0) == pdTRUE) {
    task_ = nullptr;
    finishRequest(result, now);
  }
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    wifiWasConnected_ = false;
    if (!waitingForWifiLogged_) {
      Serial.println("WEATHER: waiting for Wi-Fi");
      waitingForWifiLogged_ = true;
    }
    if (data_.valid && status_ == WeatherStatus::Ready) {
      setStatus(WeatherStatus::Stale);
    }
    return;
  }

  if (!wifiWasConnected_) {
    wifiWasConnected_ = true;
    wifiReadyAt_ = now + config::WEATHER_WIFI_SETTLE_MS;
  }
  waitingForWifiLogged_ = false;
  if (task_ != nullptr || !deadlineReached(now, nextRequestAt_) ||
      !deadlineReached(now, wifiReadyAt_)) {
    return;
  }
  startRequest(now);
}

void WeatherService::requestRefresh(unsigned long now) {
  if (!started_ || task_ != nullptr) return;
  nextRequestAt_ = now;
  wifiReadyAt_ = now;
  Serial.println("WEATHER: refresh requested");
}

bool WeatherService::getWeather(WeatherData& data) const {
  data = data_;
  return data.valid;
}

bool WeatherService::isLoading() const {
  return status_ == WeatherStatus::Loading;
}

bool WeatherService::hasError() const {
  return status_ == WeatherStatus::Error || status_ == WeatherStatus::Stale;
}

WeatherStatus WeatherService::status() const { return status_; }

uint32_t WeatherService::revision() const { return revision_; }

void WeatherService::requestTask(void* parameter) {
  WeatherService* service = static_cast<WeatherService*>(parameter);
  RequestResult result;
  char path[320];
  snprintf(
      path, sizeof(path), "/%.4f,%.4f?format=%%25t%%7C%%25f%%7C%%25h%%7C%%25w%%7C%%25C",
      WEATHER_LATITUDE, WEATHER_LONGITUDE);
  IPAddress apiAddress;
  if (WiFi.hostByName(WEATHER_HOST, apiAddress) != 1) {
    result.httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
    snprintf(result.errorDetail, sizeof(result.errorDetail), "DNS lookup failed");
  } else {
    Serial.printf("WEATHER: resolved %s to %s, RSSI=%d dBm\n", WEATHER_HOST,
                  apiAddress.toString().c_str(), WiFi.RSSI());

    WiFiClientSecure client;
    client.setInsecure();
    client.setConnectionTimeout(config::WEATHER_CONNECT_TIMEOUT_MS);
    // TLS negotiation on ESP32-S3 can take longer than the TCP connection,
    // especially while SNTP is also starting after Wi-Fi association. It still
    // runs only in this worker task, so the sensor loop remains responsive.
    client.setHandshakeTimeout(config::WEATHER_TLS_HANDSHAKE_TIMEOUT_SECONDS);

    // Connect to the address that was resolved above. Supplying WEATHER_HOST
    // separately preserves TLS SNI while avoiding a second DNS lookup inside
    // HTTPClient, which can intermittently fail just after Wi-Fi association.
    const bool tlsConnected =
        client.connect(apiAddress, WEATHER_HTTPS_PORT, WEATHER_HOST, nullptr,
                       nullptr, nullptr) == 1;
    if (tlsConnected) {
      HTTPClient http;
      http.setReuse(true);  // Reuse the already-established TLS connection.
      http.setConnectTimeout(config::WEATHER_CONNECT_TIMEOUT_MS);
      http.setTimeout(config::WEATHER_RESPONSE_TIMEOUT_MS);
      if (http.begin(client, WEATHER_HOST, WEATHER_HTTPS_PORT, path, true)) {
        result.httpCode = http.GET();
        if (result.httpCode == HTTP_CODE_OK) {
          const String body = http.getString();
          result.success = parseResponse(body, result.data);
          result.parseError = !result.success;
        } else if (result.httpCode < 0) {
          snprintf(result.errorDetail, sizeof(result.errorDetail), "%s",
                   HTTPClient::errorToString(result.httpCode).c_str());
        }
        http.end();
      } else {
        snprintf(result.errorDetail, sizeof(result.errorDetail), "%s",
                 "HTTPS client initialization failed");
        result.httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
      }
    } else {
      result.httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
      char tlsError[96] = {};
      const int tlsCode = client.lastError(tlsError, sizeof(tlsError));
      if (tlsCode != 0 && tlsError[0] != '\0') {
        snprintf(result.errorDetail, sizeof(result.errorDetail),
                 "TLS connect to %s:443 failed (%d: %s)",
                 apiAddress.toString().c_str(), tlsCode, tlsError);
      } else {
        snprintf(result.errorDetail, sizeof(result.errorDetail),
                 "TCP/TLS connect to %s:443 failed",
                 apiAddress.toString().c_str());
      }
    }
  }

  // Some networks or ESP32 TLS combinations refuse this server's TLS route.
  // The explicitly configurable retry remains inside the worker task and
  // cannot block loop().
  if (!result.success && !result.parseError && result.httpCode < 0 &&
      config::WEATHER_ALLOW_HTTP_FALLBACK &&
      WiFi.status() == WL_CONNECTED) {
    char secureError[sizeof(result.errorDetail)] = {};
    strncpy(secureError, result.errorDetail, sizeof(secureError) - 1);

    WiFiClient fallbackClient;
    fallbackClient.setConnectionTimeout(config::WEATHER_CONNECT_TIMEOUT_MS);
    if (fallbackClient.connect(apiAddress, WEATHER_HTTP_PORT,
                               config::WEATHER_CONNECT_TIMEOUT_MS)) {
      HTTPClient fallbackHttp;
      fallbackHttp.setReuse(true);
      fallbackHttp.setConnectTimeout(config::WEATHER_CONNECT_TIMEOUT_MS);
      fallbackHttp.setTimeout(config::WEATHER_RESPONSE_TIMEOUT_MS);
      if (fallbackHttp.begin(fallbackClient, WEATHER_HOST, WEATHER_HTTP_PORT,
                             path, false)) {
        const int fallbackCode = fallbackHttp.GET();
        result.httpCode = fallbackCode;
        if (fallbackCode == HTTP_CODE_OK) {
          const String body = fallbackHttp.getString();
          result.success = parseResponse(body, result.data);
          result.parseError = !result.success;
          result.usedHttpFallback = result.success;
          result.errorDetail[0] = '\0';
        } else if (fallbackCode < 0) {
          snprintf(result.errorDetail, sizeof(result.errorDetail),
                   "HTTPS: %.100s; HTTP: %s", secureError,
                   HTTPClient::errorToString(fallbackCode).c_str());
        } else {
          snprintf(result.errorDetail, sizeof(result.errorDetail),
                   "HTTPS: %.100s; HTTP status %d", secureError,
                   fallbackCode);
        }
        fallbackHttp.end();
      } else {
        snprintf(result.errorDetail, sizeof(result.errorDetail),
                 "HTTPS: %.100s; HTTP client initialization failed",
                 secureError);
        result.httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
      }
    } else {
      result.httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
      snprintf(result.errorDetail, sizeof(result.errorDetail),
               "HTTPS: %.100s; TCP connect to %s:80 failed", secureError,
               apiAddress.toString().c_str());
    }
  }

  xQueueOverwrite(service->resultQueue_, &result);
  vTaskDelete(nullptr);
}

bool WeatherService::deadlineReached(unsigned long now,
                                     unsigned long deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void WeatherService::startRequest(unsigned long now) {
  setStatus(WeatherStatus::Loading);
  Serial.println("WEATHER: request started");
  const BaseType_t created = xTaskCreate(
      requestTask, "weather-http", config::WEATHER_TASK_STACK_WORDS, this,
      config::WEATHER_TASK_PRIORITY, &task_);
  if (created != pdPASS) {
    task_ = nullptr;
    setStatus(data_.valid ? WeatherStatus::Stale : WeatherStatus::Error);
    nextRequestAt_ = now + config::WEATHER_RETRY_INTERVAL_MS;
    Serial.println("WEATHER: task creation failed");
  }
}

void WeatherService::finishRequest(const RequestResult& result,
                                   unsigned long now) {
  if (result.success) {
    data_ = result.data;
    setStatus(WeatherStatus::Ready);
    nextRequestAt_ = now + config::WEATHER_UPDATE_INTERVAL_MS;
    Serial.printf("WEATHER: updated, temp=%.1f, humidity=%u, code=%u, wind=%.1f\n",
                  data_.temperature, data_.humidity, data_.weatherCode,
                  data_.windSpeed);
    if (result.usedHttpFallback) {
      Serial.println("WEATHER: HTTPS unavailable, HTTP fallback used");
    }
    return;
  }

  setStatus(data_.valid ? WeatherStatus::Stale : WeatherStatus::Error);
  nextRequestAt_ = now + config::WEATHER_RETRY_INTERVAL_MS;
  if (result.parseError) {
    Serial.println("WEATHER: response parse error");
  } else {
    if (result.errorDetail[0] != '\0') {
      Serial.printf("WEATHER: HTTP error %d (%s)\n", result.httpCode,
                    result.errorDetail);
    } else {
      Serial.printf("WEATHER: HTTP error %d\n", result.httpCode);
    }
  }
}

void WeatherService::setStatus(WeatherStatus status) {
  if (status_ != status) {
    status_ = status;
    ++revision_;
  }
}
