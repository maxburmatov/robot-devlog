#include "VoiceCommandDetector.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cstring>

#include "AudioCapture.h"
#include "Config.h"
#include "EdgeImpulseMemory.h"
#include "EdgeImpulseVoiceModel.h"
namespace {

VoiceCommand commandForLabel(const char* label) {
  if (strcmp(label, "robot") == 0) return VoiceCommand::WakeWord;
  // The deployed Edge Impulse model names this class `privet`. Keep `hello`
  // as a compatibility alias for a future export with an English label.
  if (strcmp(label, "privet") == 0 || strcmp(label, "hello") == 0) {
    return VoiceCommand::Hello;
  }
  if (strcmp(label, "dance") == 0) return VoiceCommand::Dance;
  if (strcmp(label, "time") == 0) return VoiceCommand::Time;
  if (strcmp(label, "weather") == 0) return VoiceCommand::Weather;
  if (strcmp(label, "sleep") == 0) return VoiceCommand::Sleep;
  if (strcmp(label, "wake_up") == 0) return VoiceCommand::WakeUp;
  return VoiceCommand::None;
}

bool commandAllowed(VoiceCommand command, bool commandMode) {
  if (command == VoiceCommand::None) return false;
  return commandMode || command == VoiceCommand::WakeWord;
}

}  // namespace

const char* voiceCommandName(VoiceCommand command) {
  switch (command) {
    case VoiceCommand::None: return "none";
    case VoiceCommand::WakeWord: return "robot";
    case VoiceCommand::Hello: return "hello";
    case VoiceCommand::Dance: return "dance";
    case VoiceCommand::Time: return "time";
    case VoiceCommand::Weather: return "weather";
    case VoiceCommand::Sleep: return "sleep";
    case VoiceCommand::WakeUp: return "wake_up";
  }
  return "unknown";
}

bool VoiceCommandDetector::begin(AudioCapture& capture) {
  if (!config::VOICE_ENABLED) {
    Serial.println("VOICE: disabled in Config.h");
    return false;
  }
  if (!capture.ready()) {
    Serial.println("VOICE: unavailable because AudioCapture is offline");
    return false;
  }
  if (!reserveEdgeImpulseTensorArena() ||
      !edgeImpulseTensorArenaInPsram()) {
    Serial.println("VOICE: persistent PSRAM Tensor Arena allocation failed");
    return false;
  }
  capture_ = &capture;
  window_ = static_cast<int16_t*>(heap_caps_malloc(
      edge_impulse_voice::sampleCount() * sizeof(int16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (window_ == nullptr) {
    Serial.println("VOICE: PSRAM window allocation failed");
    return false;
  }
  edge_impulse_voice::begin();
  if (xTaskCreatePinnedToCore(taskEntry, "deskbot-voice",
                              config::VOICE_TASK_STACK_WORDS, this,
                              config::VOICE_TASK_PRIORITY, &taskHandle_,
                              config::VOICE_TASK_CORE) != pdPASS) {
    Serial.println("VOICE: task creation failed");
    return false;
  }
  ready_ = true;
  Serial.println("VOICE: Edge Impulse ready");
  Serial.printf("VOICE: Tensor Arena=%u bytes PSRAM (persistent)\n",
                static_cast<unsigned>(edgeImpulseTensorArenaBytes()));
  Serial.printf(
      "VOICE: wake=%.2f wake_single=%.2f command=%.2f command_single=%.2f confirmations=%u\n",
                config::VOICE_WAKE_THRESHOLD,
                config::VOICE_WAKE_SINGLE_WINDOW_THRESHOLD,
                config::VOICE_COMMAND_THRESHOLD,
                config::VOICE_COMMAND_SINGLE_WINDOW_THRESHOLD,
                config::VOICE_CONFIRM_WINDOWS);
  Serial.println("VOICE: MODE WAITING_WAKE_WORD");
  return true;
}

VoiceRecognition VoiceCommandDetector::takeRecognition() {
  portENTER_CRITICAL(&lock_);
  const VoiceRecognition result = pendingRecognition_;
  pendingRecognition_ = {};
  portEXIT_CRITICAL(&lock_);
  return result;
}

void VoiceCommandDetector::setCommandMode(bool enabled) {
  bool changed = false;
  portENTER_CRITICAL(&lock_);
  if (commandMode_ != enabled) {
    changed = true;
    commandMode_ = enabled;
    pendingRecognition_ = {};
    resetRequested_ = true;
    if (enabled) cooldownUntil_ = 0;
  }
  portEXIT_CRITICAL(&lock_);
  if (changed) {
    Serial.printf("VOICE: MODE %s\n",
                  enabled ? "LISTENING_COMMAND" : "WAITING_WAKE_WORD");
  }
}

void VoiceCommandDetector::setSuppressed(bool suppressed) {
  portENTER_CRITICAL(&lock_);
  const bool changed = suppressed_ != suppressed;
  suppressed_ = suppressed;
  if (changed) {
    pendingRecognition_ = {};
    resetRequested_ = true;
    if (!suppressed) requireCleanWindow_ = true;
  }
  portEXIT_CRITICAL(&lock_);
}

bool VoiceCommandDetector::ready() const { return ready_; }

uint32_t VoiceCommandDetector::droppedWindows() const {
  portENTER_CRITICAL(&lock_);
  const uint32_t result = droppedWindows_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

UBaseType_t VoiceCommandDetector::stackHighWaterMark() const {
  portENTER_CRITICAL(&lock_);
  const UBaseType_t result = stackHighWaterMark_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

VoiceDetectorStats VoiceCommandDetector::stats() const {
  portENTER_CRITICAL(&lock_);
  VoiceDetectorStats result = stats_;
  result.droppedWindows = droppedWindows_;
  result.stackHighWaterMark = stackHighWaterMark_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

void VoiceCommandDetector::taskEntry(void* context) {
  static_cast<VoiceCommandDetector*>(context)->task();
}

void VoiceCommandDetector::task() {
  const size_t windowSamples = edge_impulse_voice::sampleCount();
  const size_t stepSamples =
      config::AUDIO_INPUT_SAMPLE_RATE * config::VOICE_STEP_MS / 1000;
  bool firstInference = true;
  uint8_t consecutiveErrors = 0;
  uint64_t minimumCleanEndSample = 0;
  for (;;) {
    bool suppressed;
    bool commandMode;
    portENTER_CRITICAL(&lock_);
    suppressed = suppressed_;
    commandMode = commandMode_;
    const bool reset = resetRequested_;
    const bool requireCleanWindow = requireCleanWindow_;
    resetRequested_ = false;
    requireCleanWindow_ = false;
    portEXIT_CRITICAL(&lock_);
    if (reset) resetConfirmation(true);
    if (requireCleanWindow) {
      minimumCleanEndSample =
          capture_->totalSamples() + windowSamples;
    }
    if (suppressed) {
      vTaskDelay(pdMS_TO_TICKS(25));
      continue;
    }
    if (minimumCleanEndSample != 0 &&
        capture_->totalSamples() < minimumCleanEndSample) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    minimumCleanEndSample = 0;

    const uint64_t before = previousEndSample_;
    if (!capture_->copyLatestWindow(window_, windowSamples,
                                    previousEndSample_, stepSamples)) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (before != 0 && previousEndSample_ > before + stepSamples) {
      const uint64_t delta = previousEndSample_ - before;
      // One of the elapsed step intervals is represented by the newest window
      // copied above. Count only additional complete intervals as discarded.
      const uint32_t elapsedSteps = static_cast<uint32_t>(delta / stepSamples);
      const uint32_t missed = elapsedSteps > 0 ? elapsedSteps - 1 : 0;
      portENTER_CRITICAL(&lock_);
      droppedWindows_ += missed;
      portEXIT_CRITICAL(&lock_);
    }

    if (firstInference) {
      Serial.printf("VOICE: first inference start, stack_free=%u bytes\n",
                    static_cast<unsigned>(
                        uxTaskGetStackHighWaterMark(nullptr)));
    }
    const int64_t inferenceStartedAtUs = esp_timer_get_time();
    const edge_impulse_voice::Result result = edge_impulse_voice::infer(
        window_, windowSamples, config::VOICE_EI_RAW_DEBUG);
    const uint32_t inferenceUs = static_cast<uint32_t>(
        esp_timer_get_time() - inferenceStartedAtUs);
    const UBaseType_t stackFree = uxTaskGetStackHighWaterMark(nullptr);
    portENTER_CRITICAL(&lock_);
    stackHighWaterMark_ = stackFree;
    ++stats_.inferenceCount;
    stats_.busyTimeUs += inferenceUs;
    stats_.maxInferenceUs = max(stats_.maxInferenceUs, inferenceUs);
    stats_.lastDspMs = result.dspMs;
    stats_.lastNnMs = result.classificationMs;
    stats_.taskCore = xPortGetCoreID();
    portEXIT_CRITICAL(&lock_);
    if (firstInference) {
      Serial.printf(
          "VOICE: first inference complete, result=%d DSP=%lums NN=%lums wall=%luus stack_free=%u bytes\n",
          result.errorCode, static_cast<unsigned long>(result.dspMs),
          static_cast<unsigned long>(result.classificationMs),
          static_cast<unsigned long>(inferenceUs),
          static_cast<unsigned>(stackFree));
      firstInference = false;
    }
    // A yield alone would immediately select this priority-1 task again over
    // the idle task. Enter Blocked state briefly so idle can feed its WDT.
    vTaskDelay(pdMS_TO_TICKS(config::VOICE_POST_INFERENCE_YIELD_MS));
    if (!result.success) {
      Serial.printf("VOICE: classifier error=%d\n", result.errorCode);
      resetConfirmation(true);
      if (++consecutiveErrors >= config::VOICE_MAX_CONSECUTIVE_ERRORS) {
        Serial.printf("VOICE: paused after %u consecutive errors\n",
                      consecutiveErrors);
        vTaskDelay(pdMS_TO_TICKS(config::VOICE_ERROR_PAUSE_MS));
        consecutiveErrors = 0;
      }
      continue;
    }
    consecutiveErrors = 0;

    const unsigned long now = millis();
    VoiceCommand candidate = VoiceCommand::None;
    float candidateConfidence = 0.0F;
    for (size_t i = 0; i < result.scoreCount; ++i) {
      const VoiceCommand classified =
          commandForLabel(result.scores[i].label);
      if (commandAllowed(classified, commandMode) &&
          result.scores[i].value > candidateConfidence) {
        candidate = classified;
        candidateConfidence = result.scores[i].value;
      }
    }

    if (static_cast<long>(cooldownUntil_ - now) > 0) {
      resetConfirmation();
      continue;
    }
    const float threshold = candidate == VoiceCommand::WakeWord
                                ? config::VOICE_WAKE_THRESHOLD
                                : config::VOICE_COMMAND_THRESHOLD;
    const bool positive = candidate != VoiceCommand::None &&
                          candidateConfidence >= threshold;

    if (config::VOICE_DEBUG && candidate != VoiceCommand::None && !positive &&
        candidateConfidence >= config::VOICE_DEBUG_NEAR_THRESHOLD) {
      Serial.printf(
          "VOICE: %s_NEAR class=%s confidence=%.2f threshold=%.2f end=%llu DSP=%lums NN=%lums\n",
          commandMode ? "COMMAND" : "WAKE",
          voiceCommandName(candidate), candidateConfidence, threshold,
          static_cast<unsigned long long>(previousEndSample_),
          static_cast<unsigned long>(result.dspMs),
          static_cast<unsigned long>(result.classificationMs));
    }

    // A confirmed utterance remains latched until its class has stayed below
    // threshold long enough. This prevents overlapping windows from firing the
    // same spoken word twice.
    bool acceptForConfirmation = true;
    if (latchedCommand_ != VoiceCommand::None) {
      if (positive && candidate == latchedCommand_) {
        releaseStartedAt_ = 0;
      } else {
        if (releaseStartedAt_ == 0) releaseStartedAt_ = now;
        if (now - releaseStartedAt_ >= config::VOICE_RELEASE_MS) {
          latchedCommand_ = VoiceCommand::None;
          releaseStartedAt_ = 0;
        }
      }
      acceptForConfirmation = latchedCommand_ == VoiceCommand::None;
    }

    const bool singleWindowWake =
        !commandMode && candidate == VoiceCommand::WakeWord && positive &&
        candidateConfidence >= config::VOICE_WAKE_SINGLE_WINDOW_THRESHOLD;
    const bool singleWindowCommand =
        commandMode && candidate != VoiceCommand::WakeWord && positive &&
        candidateConfidence >=
            config::VOICE_COMMAND_SINGLE_WINDOW_THRESHOLD;
    const uint8_t requiredConfirmations =
        (singleWindowWake || singleWindowCommand)
            ? 1
            : config::VOICE_CONFIRM_WINDOWS;

    if (acceptForConfirmation && positive) {
      if (candidate == confirmationCommand_ &&
          now - confirmationStartedAt_ <= config::VOICE_CONFIRM_SPAN_MS) {
        ++confirmedWindows_;
      } else {
        confirmationCommand_ = candidate;
        confirmedWindows_ = 1;
        confirmationStartedAt_ = now;
      }
      if (config::VOICE_DEBUG) {
        const uint64_t windowAdvance =
            before == 0 ? 0 : previousEndSample_ - before;
        Serial.printf(
            "VOICE: %s class=%s candidate=%.2f confirmation=%u/%u end=%llu advance=%llu DSP=%lums NN=%lums wall=%luus missed=%lu\n",
                      candidate == VoiceCommand::WakeWord ? "WAKE" : "COMMAND",
                      voiceCommandName(candidate), candidateConfidence,
                      confirmedWindows_,
                      requiredConfirmations,
                      static_cast<unsigned long long>(previousEndSample_),
                      static_cast<unsigned long long>(windowAdvance),
                      static_cast<unsigned long>(result.dspMs),
                      static_cast<unsigned long>(result.classificationMs),
                      static_cast<unsigned long>(inferenceUs),
                      static_cast<unsigned long>(droppedWindows_));
      }
    } else if (confirmedWindows_ != 0 &&
               now - confirmationStartedAt_ > config::VOICE_CONFIRM_SPAN_MS) {
      resetConfirmation();
    }
    if (confirmedWindows_ >= requiredConfirmations) {
      portENTER_CRITICAL(&lock_);
      if (!suppressed_ && pendingRecognition_.command == VoiceCommand::None) {
        pendingRecognition_.command = candidate;
        pendingRecognition_.confidence = candidateConfidence;
      }
      portEXIT_CRITICAL(&lock_);
      cooldownUntil_ = now + config::VOICE_RECOGNITION_COOLDOWN_MS;
      latchedCommand_ = candidate;
      releaseStartedAt_ = 0;
      Serial.printf("VOICE: %s CONFIRMED %s confidence=%.2f\n",
                    candidate == VoiceCommand::WakeWord ? "WAKE" : "COMMAND",
                    voiceCommandName(candidate), candidateConfidence);
      resetConfirmation();
    }

  }
}

void VoiceCommandDetector::resetConfirmation(bool resetWindow) {
  confirmationCommand_ = VoiceCommand::None;
  confirmedWindows_ = 0;
  confirmationStartedAt_ = 0;
  if (resetWindow) previousEndSample_ = 0;
}
