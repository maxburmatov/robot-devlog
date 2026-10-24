#pragma once

#include <Arduino.h>
#include <time.h>

// Non-blocking Wi-Fi/NTP service. Once SNTP has set the ESP32 system clock,
// rendering reads local time directly and does not perform network requests.
class TimeService {
 public:
  void begin(unsigned long now);
  void update(unsigned long now);
  bool getLocalTime(tm& localTime) const;

 private:
  enum class Phase {
    Disabled,
    Connecting,
    Synchronizing,
    Synchronized,
    RetryWait,
  };

  void startWifi(unsigned long now);
  void startSynchronization(unsigned long now);
  void scheduleRetry(unsigned long now);
  static bool deadlineReached(unsigned long now, unsigned long deadline);
  static bool credentialsConfigured();
  static bool systemClockIsValid();

  Phase phase_ = Phase::Disabled;
  unsigned long deadline_ = 0;
  bool synchronized_ = false;
};
