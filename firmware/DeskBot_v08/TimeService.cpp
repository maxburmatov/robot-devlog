#include "TimeService.h"

#include <WiFi.h>
#include <cstring>
#include <esp_sntp.h>

#include "Config.h"
#include "Secrets.h"

namespace {

constexpr time_t MINIMUM_VALID_TIME = 1577836800;  // 2020-01-01 UTC
volatile bool ntpTimeAdjusted = false;

void onNtpTimeAdjusted(timeval*) {
  ntpTimeAdjusted = true;
}

}  // namespace

void TimeService::begin(unsigned long now) {
  // Set Moscow's POSIX timezone immediately. configTzTime() applies the same
  // value again when Wi-Fi is available and SNTP is started.
  setenv("TZ", config::TIMEZONE, 1);
  tzset();

  if (!credentialsConfigured()) {
    Serial.println("WIFI: unavailable");
    phase_ = Phase::Disabled;
    return;
  }
  startWifi(now);
}

void TimeService::update(unsigned long now) {
  switch (phase_) {
    case Phase::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WIFI: connected");
        startSynchronization(now);
      } else if (deadlineReached(now, deadline_)) {
        Serial.println("WIFI: unavailable");
        WiFi.disconnect();
        scheduleRetry(now);
      }
      break;

    case Phase::Synchronizing:
      if (ntpTimeAdjusted && systemClockIsValid()) {
        synchronized_ = true;
        phase_ = Phase::Synchronized;
        Serial.println("TIME: synchronized");

        tm localTime = {};
        if (getLocalTime(localTime)) {
          char timestamp[24];
          snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d",
                   localTime.tm_year + 1900, localTime.tm_mon + 1,
                   localTime.tm_mday, localTime.tm_hour, localTime.tm_min);
          Serial.printf("TIME: %s\n", timestamp);
        }
      } else if (deadlineReached(now, deadline_)) {
        Serial.println("TIME: synchronization failed");
        scheduleRetry(now);
      }
      break;

    case Phase::RetryWait:
      if (deadlineReached(now, deadline_)) {
        if (WiFi.status() == WL_CONNECTED) {
          startSynchronization(now);
        } else {
          startWifi(now);
        }
      }
      break;

    case Phase::Disabled:
    case Phase::Synchronized:
      break;
  }
}

bool TimeService::getLocalTime(tm& localTime) const {
  if (!synchronized_) {
    return false;
  }
  const time_t now = time(nullptr);
  if (now < MINIMUM_VALID_TIME) {
    return false;
  }
  return localtime_r(&now, &localTime) != nullptr;
}

void TimeService::startWifi(unsigned long now) {
  Serial.println("WIFI: connecting");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  phase_ = Phase::Connecting;
  deadline_ = now + config::WIFI_CONNECT_TIMEOUT_MS;
}

void TimeService::startSynchronization(unsigned long now) {
  Serial.println("TIME: synchronizing");
  ntpTimeAdjusted = false;
  sntp_set_time_sync_notification_cb(onNtpTimeAdjusted);
  configTzTime(config::TIMEZONE, config::NTP_SERVER_1,
               config::NTP_SERVER_2);
  phase_ = Phase::Synchronizing;
  deadline_ = now + config::TIME_SYNC_TIMEOUT_MS;
}

void TimeService::scheduleRetry(unsigned long now) {
  phase_ = Phase::RetryWait;
  deadline_ = now + config::TIME_RETRY_INTERVAL_MS;
}

bool TimeService::deadlineReached(unsigned long now, unsigned long deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

bool TimeService::credentialsConfigured() {
  return WIFI_SSID[0] != '\0' &&
         std::strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

bool TimeService::systemClockIsValid() {
  return time(nullptr) >= MINIMUM_VALID_TIME;
}
