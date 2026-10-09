#pragma once

#include <Arduino.h>
#include <esp_adc/adc_continuous.h>

#include "Config.h"
#include "MusicAnalyzer.h"

enum class MicrophoneEvent {
  None,
  Sound,
  Clap,
};

class MicrophoneSensor {
 public:
  bool begin(MusicAnalyzer& musicAnalyzer);
  MicrophoneEvent update(unsigned long now);

  // Sampling continues while suppressed, but classifier state and events are
  // discarded. Changing either direction starts with clean histories.
  void setSuppressed(bool suppressed);

  int amplitude() const;
  int rise() const;
  int preAverage() const;

 private:
  static constexpr size_t DMA_BUFFER_BYTES =
      config::MUSIC_FFT_SIZE * sizeof(uint32_t);

  struct SampleWindow {
    int minimum;
    int maximum;
    uint16_t sampleCount;
    unsigned long endedAt;
    uint32_t generation;
  };

  static void samplingTaskEntry(void* context);
  void samplingTask();
  MicrophoneEvent finishWindow(const SampleWindow& window);
  void processDiagnosticWindows();
  void pushAmplitude(int value);
  void pushSoundActivity(bool active);
  void resetClassifier();
  bool cooldownActive(unsigned long now) const;

  TaskHandle_t samplingTaskHandle_ = nullptr;
  QueueHandle_t analysisQueue_ = nullptr;
  QueueHandle_t diagnosticQueue_ = nullptr;
  adc_continuous_handle_t adcHandle_ = nullptr;
  MusicAnalyzer* musicAnalyzer_ = nullptr;
  alignas(4) uint8_t dmaBuffer_[DMA_BUFFER_BYTES] = {};
  uint16_t audioFrame_[config::MUSIC_FFT_SIZE] = {};
  mutable portMUX_TYPE samplingLock_ = portMUX_INITIALIZER_UNLOCKED;
  uint32_t samplingGeneration_ = 0;
  bool initialized_ = false;

  int previousDiagnosticAmplitude_ = 0;
  unsigned long previousDiagnosticEndedAt_ = 0;
  uint8_t diagnosticTailWindows_ = 0;

  int amplitude_ = 0;
  int previousAmplitude_ = 0;
  int rise_ = 0;
  int preAverage_ = 0;

  int amplitudeHistory_[config::MIC_CLAP_HISTORY_SIZE] = {};
  uint8_t amplitudeHistoryIndex_ = 0;
  uint8_t amplitudeHistoryCount_ = 0;

  bool soundHistory_[config::MIC_SOUND_HISTORY_SIZE] = {};
  uint8_t soundHistoryIndex_ = 0;
  uint8_t soundHistoryCount_ = 0;

  bool clapCandidate_ = false;
  unsigned long clapCandidateStartedAt_ = 0;
  bool suppressed_ = false;
  bool hasEmittedEvent_ = false;
  unsigned long lastEventAt_ = 0;
};
