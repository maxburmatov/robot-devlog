#include "SoundSystem.h"

#include <esp_system.h>
#include <esp_timer.h>

#include "Config.h"
#include "DeskBotClapSound.h"
#include "DeskBotCommandSounds.h"
#include "DeskBotIdleSounds.h"
#include "DeskBotSounds.h"

namespace {

struct SoundClip {
  const int16_t* samples;
  size_t sampleCount;
};

const SoundClip kWaitingClips[] = {
    {SOUND_WAITING, SOUND_WAITING_SAMPLES},
    {DB_IDLE_WAITING_2, DB_IDLE_WAITING_2_SAMPLES},
};

const SoundClip kBoredClips[] = {
    {SOUND_BORED, SOUND_BORED_SAMPLES},
    {DB_IDLE_BORED_1, DB_IDLE_BORED_1_SAMPLES},
    {DB_IDLE_BORED_2, DB_IDLE_BORED_2_SAMPLES},
};

// The three supplied idle clips fully replace the original Content sound.
const SoundClip kContentClips[] = {
    {DB_IDLE_CONTENT_2, DB_IDLE_CONTENT_2_SAMPLES},
    {DB_IDLE_CONTENT_3, DB_IDLE_CONTENT_3_SAMPLES},
    {DB_IDLE_CONTENT_5, DB_IDLE_CONTENT_5_SAMPLES},
};

uint8_t variantCountFor(RobotState state) {
  switch (state) {
    case RobotState::Waiting:
      return sizeof(kWaitingClips) / sizeof(kWaitingClips[0]);
    case RobotState::Bored:
      return sizeof(kBoredClips) / sizeof(kBoredClips[0]);
    case RobotState::Content:
      return sizeof(kContentClips) / sizeof(kContentClips[0]);
    case RobotState::Curious:
    case RobotState::Offended:
    case RobotState::Reconciling:
    case RobotState::Happy:
    case RobotState::Drowsy:
    case RobotState::Sleep:
    case RobotState::Wake:
    case RobotState::Surprised:
    case RobotState::Startled:
    case RobotState::PickedUp:
    case RobotState::Dizzy:
    case RobotState::UpsideDown:
    case RobotState::Attention:
    case RobotState::Listening:
    case RobotState::Dancing:
    case RobotState::Greeting:
    case RobotState::Count:
      return 1;
  }
  return 1;
}

SoundClip clipFor(RobotState state, uint8_t variant) {
  const uint8_t index = variant > 0 ? variant - 1 : 0;
  switch (state) {
    case RobotState::Curious:
      return {SOUND_CURIOUS, SOUND_CURIOUS_SAMPLES};
    case RobotState::Waiting:
      return kWaitingClips[index % variantCountFor(state)];
    case RobotState::Bored:
      return kBoredClips[index % variantCountFor(state)];
    case RobotState::Offended:
      return {SOUND_OFFENDED, SOUND_OFFENDED_SAMPLES};
    case RobotState::Reconciling:
      return {SOUND_RECONCILING, SOUND_RECONCILING_SAMPLES};
    case RobotState::Happy:
      return {SOUND_HAPPY, SOUND_HAPPY_SAMPLES};
    case RobotState::Content:
      return kContentClips[index % variantCountFor(state)];
    case RobotState::Drowsy:
      return {SOUND_DROWSY, SOUND_DROWSY_SAMPLES};
    case RobotState::Wake:
      return {SOUND_WAKE, SOUND_WAKE_SAMPLES};
    case RobotState::Surprised:
      return {SOUND_SURPRISED, SOUND_SURPRISED_SAMPLES};
    case RobotState::PickedUp:
      return {SOUND_PICKED_UP, SOUND_PICKED_UP_SAMPLES};
    case RobotState::Dizzy:
      return {SOUND_DIZZY, SOUND_DIZZY_SAMPLES};
    case RobotState::UpsideDown:
      return {SOUND_UPSIDE_DOWN, SOUND_UPSIDE_DOWN_SAMPLES};
    case RobotState::Sleep:
    case RobotState::Startled:
    case RobotState::Attention:
    case RobotState::Listening:
    case RobotState::Dancing:
    case RobotState::Greeting:
    case RobotState::Count:
      return {nullptr, 0};
  }
  return {nullptr, 0};
}

static_assert(config::AUDIO_SAMPLE_RATE == DESKBOT_SOUND_SAMPLE_RATE,
              "Configured sample rate must match the embedded PCM data");
static_assert(config::AUDIO_SAMPLE_RATE == DESKBOT_IDLE_SOUND_SAMPLE_RATE,
              "Configured sample rate must match the idle PCM data");
static_assert(config::AUDIO_SAMPLE_RATE == DESKBOT_CLAP_SOUND_SAMPLE_RATE,
              "Configured sample rate must match the clap PCM data");
static_assert(config::AUDIO_SAMPLE_RATE == DESKBOT_COMMAND_SOUND_SAMPLE_RATE,
              "Configured sample rate must match the command PCM data");

SoundClip cueClip(InterfaceCue cue) {
  switch (cue) {
    case InterfaceCue::Hello:
      return {SOUND_COMMAND_HELLO, SOUND_COMMAND_HELLO_SAMPLES};
    case InterfaceCue::Done:
      return {SOUND_COMMAND_DONE, SOUND_COMMAND_DONE_SAMPLES};
    case InterfaceCue::Listening:
      return {SOUND_LISTENING_CUE, SOUND_LISTENING_CUE_SAMPLES};
  }
  return {nullptr, 0};
}

int16_t scaleSample(int16_t sample) {
  int32_t scaled = static_cast<int32_t>(sample * config::AUDIO_VOLUME);
  if (scaled > INT16_MAX) {
    scaled = INT16_MAX;
  } else if (scaled < INT16_MIN) {
    scaled = INT16_MIN;
  }
  return static_cast<int16_t>(scaled);
}

}  // namespace

bool SoundSystem::begin() {
  enabled_ = config::AUDIO_ENABLED;
  if (!config::AUDIO_ENABLED) {
    Serial.println("AUDIO: disabled in Config.h");
    return true;
  }

  const i2s_config_t i2sConfig = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = config::AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = config::AUDIO_DMA_BUFFER_COUNT,
      .dma_buf_len = config::AUDIO_DMA_BUFFER_SAMPLES,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };

  const i2s_pin_config_t pins = {
      .bck_io_num = config::I2S_BCLK_PIN,
      .ws_io_num = config::I2S_LRC_PIN,
      .data_out_num = config::I2S_DOUT_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  esp_err_t result = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, nullptr);
  if (result == ESP_OK) {
    result = i2s_set_pin(I2S_NUM_0, &pins);
  }
  if (result == ESP_OK) {
    result = i2s_zero_dma_buffer(I2S_NUM_0);
  }
  if (result != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    Serial.printf("AUDIO: legacy I2S initialization failed (%d)\n", result);
    return false;
  }

  randomSeed(esp_random());
  requestQueue_ = xQueueCreate(1, sizeof(Request));
  if (requestQueue_ == nullptr) {
    i2s_driver_uninstall(I2S_NUM_0);
    Serial.println("AUDIO: queue creation failed");
    return false;
  }

  const BaseType_t taskCreated = xTaskCreatePinnedToCore(
      audioTaskEntry, "deskbot-audio", config::AUDIO_TASK_STACK_WORDS,
      this, config::AUDIO_TASK_PRIORITY, &taskHandle_, config::AUDIO_TASK_CORE);
  if (taskCreated != pdPASS) {
    i2s_driver_uninstall(I2S_NUM_0);
    vQueueDelete(requestQueue_);
    requestQueue_ = nullptr;
    taskHandle_ = nullptr;
    Serial.println("AUDIO: task creation failed");
    return false;
  }

  Serial.println("AUDIO: MAX98357A legacy I2S path initialized");
  Serial.printf("AUDIO: %lu Hz / PCM16 / 13 states / 19 clips / volume=%.2f\n",
                static_cast<unsigned long>(config::AUDIO_SAMPLE_RATE),
                config::AUDIO_VOLUME);
  initialized_ = true;
  return true;
}

bool SoundSystem::play(RobotState state) {
  const SoundClip clip = clipFor(state, 1);
  if (!config::AUDIO_ENABLED || requestQueue_ == nullptr ||
      clip.samples == nullptr) {
    return false;
  }

  const uint8_t variant = chooseVariant(state);
  uint32_t sequence = 0;
  portENTER_CRITICAL(&lock_);
  if (!enabled_) {
    portEXIT_CRITICAL(&lock_);
    return false;
  }
  sequence = ++nextSequence_;
  if (sequence == 0) {
    sequence = ++nextSequence_;
  }
  pendingSequence_ = sequence;
  if (currentSequence_ != 0) {
    abortRequested_ = true;
  }
  portEXIT_CRITICAL(&lock_);

  const Request request = {RequestType::State, state, variant,
                           InterfaceCue::Done, sequence};
  xQueueOverwrite(requestQueue_, &request);
  Serial.printf("AUDIO: %s variant=%u/%u\n", stateName(state), variant,
                variantCountFor(state));
  return true;
}

bool SoundSystem::playClapReaction() {
  if (!config::AUDIO_ENABLED || requestQueue_ == nullptr) {
    return false;
  }

  uint32_t sequence = 0;
  portENTER_CRITICAL(&lock_);
  if (!enabled_ || currentSequence_ != 0 || pendingSequence_ != 0) {
    portEXIT_CRITICAL(&lock_);
    return false;
  }
  sequence = ++nextSequence_;
  if (sequence == 0) {
    sequence = ++nextSequence_;
  }
  pendingSequence_ = sequence;
  portEXIT_CRITICAL(&lock_);

  const Request request = {RequestType::ClapReaction, RobotState::Startled,
                           1, InterfaceCue::Done, sequence};
  xQueueOverwrite(requestQueue_, &request);
  Serial.println("AUDIO: CLAP_STARTLED variant=1/1");
  return true;
}

bool SoundSystem::playCue(InterfaceCue cue) {
  if (!config::AUDIO_ENABLED || requestQueue_ == nullptr ||
      cueClip(cue).samples == nullptr) {
    return false;
  }
  uint32_t sequence = 0;
  portENTER_CRITICAL(&lock_);
  if (!enabled_ || currentSequence_ != 0 || pendingSequence_ != 0) {
    portEXIT_CRITICAL(&lock_);
    return false;
  }
  sequence = ++nextSequence_;
  if (sequence == 0) sequence = ++nextSequence_;
  pendingSequence_ = sequence;
  portEXIT_CRITICAL(&lock_);
  const Request request = {RequestType::InterfaceCue, RobotState::Count, 1,
                           cue, sequence};
  xQueueOverwrite(requestQueue_, &request);
  return true;
}

void SoundSystem::setEnabled(bool enabled) {
  const bool requested = config::AUDIO_ENABLED && enabled;
  bool changed = false;
  portENTER_CRITICAL(&lock_);
  if (enabled_ != requested) {
    enabled_ = requested;
    changed = true;
  }
  if (!enabled_) {
    pendingSequence_ = 0;
    abortRequested_ = currentSequence_ != 0;
  } else if (currentSequence_ == 0) {
    abortRequested_ = false;
  }
  portEXIT_CRITICAL(&lock_);

  if (!requested && requestQueue_ != nullptr) {
    xQueueReset(requestQueue_);
  }
  if (!requested && initialized_) {
    i2s_zero_dma_buffer(I2S_NUM_0);
  }
  if (changed) {
    Serial.printf("AUDIO: runtime %s\n", requested ? "enabled" : "disabled");
  }
}

bool SoundSystem::isEnabled() const {
  portENTER_CRITICAL(&lock_);
  const bool enabled = enabled_;
  portEXIT_CRITICAL(&lock_);
  return enabled;
}

bool SoundSystem::isPlaying() const {
  portENTER_CRITICAL(&lock_);
  const bool playing = currentSequence_ != 0;
  portEXIT_CRITICAL(&lock_);
  return playing;
}

SoundSystemStats SoundSystem::stats() const {
  portENTER_CRITICAL(&lock_);
  const SoundSystemStats result = stats_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

void SoundSystem::audioTaskEntry(void* context) {
  static_cast<SoundSystem*>(context)->audioTask();
}

void SoundSystem::audioTask() {
  for (;;) {
    Request request;
    if (xQueueReceive(requestQueue_, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    portENTER_CRITICAL(&lock_);
    if (!enabled_) {
      pendingSequence_ = 0;
      abortRequested_ = false;
      portEXIT_CRITICAL(&lock_);
      continue;
    }
    currentSequence_ = request.sequence;
    if (pendingSequence_ == request.sequence) {
      pendingSequence_ = 0;
    }
    abortRequested_ = pendingSequence_ != 0;
    portEXIT_CRITICAL(&lock_);

    const int64_t startedAtUs = esp_timer_get_time();
    playRequest(request);

    portENTER_CRITICAL(&lock_);
    ++stats_.playedRequests;
    stats_.busyTimeUs += esp_timer_get_time() - startedAtUs;
    stats_.taskCore = xPortGetCoreID();
    stats_.stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
    if (currentSequence_ == request.sequence) {
      currentSequence_ = 0;
    }
    portEXIT_CRITICAL(&lock_);
  }
}

void SoundSystem::playRequest(const Request& request) {
  const SoundClip clip = request.type == RequestType::ClapReaction
                             ? SoundClip{SOUND_CLAP_STARTLED,
                                         SOUND_CLAP_STARTLED_SAMPLES}
                         : request.type == RequestType::InterfaceCue
                             ? cueClip(request.cue)
                             : clipFor(request.state, request.variant);
  int16_t stereoBuffer[config::AUDIO_DMA_BUFFER_SAMPLES * 2];

  for (size_t position = 0; position < clip.sampleCount;) {
    portENTER_CRITICAL(&lock_);
    const bool shouldAbort = abortRequested_;
    portEXIT_CRITICAL(&lock_);
    if (shouldAbort) {
      i2s_zero_dma_buffer(I2S_NUM_0);
      return;
    }

    const size_t remaining = clip.sampleCount - position;
    const size_t monoCount = min(
        remaining, static_cast<size_t>(config::AUDIO_DMA_BUFFER_SAMPLES));
    for (size_t i = 0; i < monoCount; ++i) {
      const int16_t sample = scaleSample(clip.samples[position + i]);
      stereoBuffer[i * 2] = sample;
      stereoBuffer[i * 2 + 1] = sample;
    }

    size_t bytesWritten = 0;
    const size_t bytesToWrite = monoCount * 2 * sizeof(stereoBuffer[0]);
    const esp_err_t result = i2s_write(I2S_NUM_0, stereoBuffer, bytesToWrite,
                                       &bytesWritten, portMAX_DELAY);
    if (result != ESP_OK || bytesWritten != bytesToWrite) {
      Serial.printf("AUDIO: I2S write error=%d written=%u expected=%u\n",
                    result, static_cast<unsigned>(bytesWritten),
                    static_cast<unsigned>(bytesToWrite));
      break;
    }
    position += monoCount;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
}

uint8_t SoundSystem::chooseVariant(RobotState state) {
  const uint8_t count = variantCountFor(state);
  const uint8_t index = static_cast<uint8_t>(state);
  const uint8_t previous = lastVariant_[index];
  uint8_t variant = static_cast<uint8_t>(random(1, count + 1));
  if (count > 1 && variant == previous) {
    variant = static_cast<uint8_t>((variant % count) + 1);
  }
  lastVariant_[index] = variant;
  return variant;
}

const char* SoundSystem::stateName(RobotState state) {
  switch (state) {
    case RobotState::Curious:
      return "CURIOUS";
    case RobotState::Waiting:
      return "WAITING";
    case RobotState::Bored:
      return "BORED";
    case RobotState::Offended:
      return "OFFENDED";
    case RobotState::Reconciling:
      return "RECONCILING";
    case RobotState::Happy:
      return "HAPPY";
    case RobotState::Content:
      return "CONTENT";
    case RobotState::Drowsy:
      return "DROWSY";
    case RobotState::Sleep:
      return "SLEEP";
    case RobotState::Wake:
      return "WAKE";
    case RobotState::Surprised:
      return "SURPRISED";
    case RobotState::Startled:
      return "STARTLED";
    case RobotState::PickedUp:
      return "PICKED_UP";
    case RobotState::Dizzy:
      return "DIZZY";
    case RobotState::UpsideDown:
      return "UPSIDE_DOWN";
    case RobotState::Attention:
      return "ATTENTION";
    case RobotState::Listening:
      return "LISTENING";
    case RobotState::Dancing:
      return "DANCING";
    case RobotState::Greeting:
      return "GREETING";
    case RobotState::Count:
      return "COUNT";
  }
  return "UNKNOWN";
}
