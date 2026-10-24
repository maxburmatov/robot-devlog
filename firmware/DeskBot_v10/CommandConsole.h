#pragma once

#include <Arduino.h>

class GazeTracker;
class LightMonitor;
class LightingAutomation;
class NeckBehaviorController;
class NeckController;
class TuyaPlugClient;

struct CommandConsoleEffects {
  bool manualPlugRequest = false;
  bool plugStatusRequest = false;
};

// Owns the line-oriented diagnostic command protocol. Keeping parsing here
// prevents the application loop from growing every time a subsystem gains a
// new maintenance command.
class CommandConsole {
 public:
  CommandConsole(GazeTracker& gazeTracker, NeckController& neck,
                 NeckBehaviorController& neckBehavior,
                 LightMonitor& lightMonitor,
                 LightingAutomation& lightingAutomation,
                 TuyaPlugClient& tuyaPlug);

  CommandConsoleEffects service(Stream& input, Print& output, uint32_t nowMs);

 private:
  static constexpr size_t kCommandCapacity = 48;

  void handleCommand(const char* command, Print& output, uint32_t nowMs,
                     CommandConsoleEffects& effects);
  bool handleGazeCommand(const char* command, Print& output, uint32_t nowMs);
  bool handleNeckCommand(const char* command, Print& output, uint32_t nowMs);
  void printLightStatus(Print& output, uint32_t nowMs) const;
  void printPlugInfo(Print& output) const;
  void printNeckStatus(Print& output, uint32_t nowMs) const;

  static bool parseFloat(const char*& cursor, float& value);
  static bool commandEnds(const char* cursor);
  static bool angleInRange(float value, float minimum, float maximum);

  GazeTracker& gazeTracker_;
  NeckController& neck_;
  NeckBehaviorController& neckBehavior_;
  LightMonitor& lightMonitor_;
  LightingAutomation& lightingAutomation_;
  TuyaPlugClient& tuyaPlug_;
  char command_[kCommandCapacity] = {};
  size_t commandLength_ = 0;
};
