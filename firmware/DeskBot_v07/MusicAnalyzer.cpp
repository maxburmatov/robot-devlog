#include "MusicAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static_assert(config::MUSIC_FFT_SIZE >= 64 && config::MUSIC_FFT_SIZE <= 1024 &&
                  (config::MUSIC_FFT_SIZE & (config::MUSIC_FFT_SIZE - 1)) == 0,
              "MUSIC_FFT_SIZE must be a power of two from 64 to 1024");
static_assert(config::MIC_AUDIO_SAMPLE_RATE >= 8000,
              "Music spectrum requires at least 8 kHz sampling");
static_assert(config::MIC_AUDIO_SAMPLE_RATE / 2 >= 4000,
              "Sample rate must cover the highest equalizer band");
static_assert(config::MUSIC_MIN_BPM > 0 &&
                  config::MUSIC_MIN_BPM < config::MUSIC_MAX_BPM,
              "Invalid music BPM range");
static_assert(config::MUSIC_BAND_COUNT == 8,
              "The OLED equalizer layout requires eight bands");
static_assert(config::MUSIC_REQUIRED_BEATS >= 6,
              "Music confirmation needs at least six beats");

namespace {
constexpr UBaseType_t FRAME_QUEUE_LENGTH = 3;
constexpr float PI_F = 3.14159265358979323846F;
constexpr float BAND_EDGES[config::MUSIC_BAND_COUNT + 1] = {
    60.0F, 140.0F, 250.0F, 450.0F, 750.0F,
    1200.0F, 1900.0F, 2800.0F, 4000.0F};

void fft(float* real, float* imag, size_t count) {
  for (size_t i = 1, j = 0; i < count; ++i) {
    size_t bit = count >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(real[i], real[j]);
      std::swap(imag[i], imag[j]);
    }
  }
  for (size_t length = 2; length <= count; length <<= 1) {
    const float angle = -2.0F * PI_F / static_cast<float>(length);
    const float wlenReal = cosf(angle);
    const float wlenImag = sinf(angle);
    for (size_t base = 0; base < count; base += length) {
      float wr = 1.0F;
      float wi = 0.0F;
      for (size_t j = 0; j < length / 2; ++j) {
        const size_t even = base + j;
        const size_t odd = even + length / 2;
        const float vr = real[odd] * wr - imag[odd] * wi;
        const float vi = real[odd] * wi + imag[odd] * wr;
        const float ur = real[even];
        const float ui = imag[even];
        real[even] = ur + vr;
        imag[even] = ui + vi;
        real[odd] = ur - vr;
        imag[odd] = ui - vi;
        const float nextWr = wr * wlenReal - wi * wlenImag;
        wi = wr * wlenImag + wi * wlenReal;
        wr = nextWr;
      }
    }
  }
}
}  // namespace

bool MusicAnalyzer::begin() {
  frameQueue_ = xQueueCreate(FRAME_QUEUE_LENGTH, sizeof(AudioFrame));
  if (frameQueue_ == nullptr) return false;
  return xTaskCreate(taskEntry, "deskbot-music", config::MUSIC_TASK_STACK_WORDS,
                     this, config::MUSIC_TASK_PRIORITY, &taskHandle_) == pdPASS;
}

bool MusicAnalyzer::submitFrame(const uint16_t* samples, unsigned long endedAt,
                                uint32_t generation) {
  if (frameQueue_ == nullptr) return false;
  AudioFrame frame;
  memcpy(frame.samples, samples, sizeof(frame.samples));
  frame.endedAt = endedAt;
  frame.generation = generation;
  if (xQueueSend(frameQueue_, &frame, 0) == pdTRUE) return true;
  portENTER_CRITICAL(&lock_);
  ++published_.droppedFrames;
  portEXIT_CRITICAL(&lock_);
  return false;
}

void MusicAnalyzer::setSuppressed(bool suppressed, uint32_t generation) {
  portENTER_CRITICAL(&lock_);
  const bool changed = suppressed_ != suppressed || generation_ != generation;
  suppressed_ = suppressed;
  generation_ = generation;
  portEXIT_CRITICAL(&lock_);
  if (changed && frameQueue_ != nullptr) xQueueReset(frameQueue_);
}

MusicSnapshot MusicAnalyzer::snapshot() const {
  portENTER_CRITICAL(&lock_);
  const MusicSnapshot result = published_;
  portEXIT_CRITICAL(&lock_);
  return result;
}

void MusicAnalyzer::taskEntry(void* context) {
  static_cast<MusicAnalyzer*>(context)->task();
}

void MusicAnalyzer::task() {
  AudioFrame frame;
  for (;;) {
    if (xQueueReceive(frameQueue_, &frame, portMAX_DELAY) != pdTRUE) continue;
    portENTER_CRITICAL(&lock_);
    const bool accept = !suppressed_ && frame.generation == generation_;
    portEXIT_CRITICAL(&lock_);
    if (!accept) {
      resetDetector();
      continue;
    }
    analyze(frame);
  }
}

void MusicAnalyzer::resetDetector() {
  noiseFloor_ = 0.0F;
  lastBeatAt_ = 0;
  lastBeatIntervalMs_ = 0;
  lastStrongEnergyAt_ = 0;
  lastRegularAt_ = 0;
  beatCount_ = 0;
  beatIndex_ = 0;
  active_ = false;
  memset(smoothedBands_, 0, sizeof(smoothedBands_));
  memset(noiseBands_, 0, sizeof(noiseBands_));
  memset(peakBands_, 0, sizeof(peakBands_));
  memset(previousBandEnergies_, 0, sizeof(previousBandEnergies_));
  portENTER_CRITICAL(&lock_);
  published_.active = false;
  published_.candidate = false;
  published_.bpm = 0;
  published_.confidence = 0.0F;
  memset(published_.bands, 0, sizeof(published_.bands));
  portEXIT_CRITICAL(&lock_);
}

bool MusicAnalyzer::rhythmIsRegular(uint16_t& bpm, float& confidence) const {
  if (beatCount_ < config::MUSIC_REQUIRED_BEATS) return false;
  constexpr uint8_t intervalCount = config::MUSIC_REQUIRED_BEATS - 1;
  unsigned long ordered[config::MUSIC_REQUIRED_BEATS];
  for (uint8_t i = 0; i < config::MUSIC_REQUIRED_BEATS; ++i) {
    ordered[i] = beatTimes_[(beatIndex_ + i) % config::MUSIC_REQUIRED_BEATS];
  }
  unsigned long intervals[intervalCount];
  for (uint8_t i = 0; i < intervalCount; ++i) {
    intervals[i] = ordered[i + 1] - ordered[i];
  }
  unsigned long sorted[intervalCount];
  memcpy(sorted, intervals, sizeof(sorted));
  std::sort(sorted, sorted + intervalCount);
  const unsigned long median = sorted[intervalCount / 2];
  if (median == 0) return false;
  uint16_t candidate = static_cast<uint16_t>(60000UL / median);
  // Strong hi-hats often produce two onsets per musical beat. Fold that
  // regular double tempo back into the supported musical range.
  while (candidate > config::MUSIC_MAX_BPM) candidate /= 2;
  while (candidate < config::MUSIC_MIN_BPM) candidate *= 2;
  if (candidate < config::MUSIC_MIN_BPM || candidate > config::MUSIC_MAX_BPM)
    return false;
  uint8_t close = 0;
  float deviation = 0.0F;
  for (uint8_t i = 0; i < intervalCount; ++i) {
    const float relative = fabsf(static_cast<float>(intervals[i]) - median) /
                           static_cast<float>(median);
    if (relative <= config::MUSIC_INTERVAL_TOLERANCE) ++close;
    deviation += relative;
  }
  bpm = candidate;
  confidence = 1.0F - deviation / intervalCount;
  return close >= 4 && ordered[config::MUSIC_REQUIRED_BEATS - 1] - ordered[0] <= 5200;
}

void MusicAnalyzer::analyze(const AudioFrame& frame) {
  float mean = 0.0F;
  for (size_t i = 0; i < config::MUSIC_FFT_SIZE; ++i) mean += frame.samples[i];
  mean /= config::MUSIC_FFT_SIZE;
  for (size_t i = 0; i < config::MUSIC_FFT_SIZE; ++i) {
    const float window = 0.5F - 0.5F * cosf(2.0F * PI_F * i /
                                           (config::MUSIC_FFT_SIZE - 1));
    real_[i] = (static_cast<float>(frame.samples[i]) - mean) * window;
    imag_[i] = 0.0F;
  }
  fft(real_, imag_, config::MUSIC_FFT_SIZE);

  float energies[config::MUSIC_BAND_COUNT] = {};
  for (size_t bin = 1; bin <= config::MUSIC_FFT_SIZE / 2; ++bin) {
    const float frequency = static_cast<float>(bin) * config::MIC_AUDIO_SAMPLE_RATE /
                            config::MUSIC_FFT_SIZE;
    const float energy = real_[bin] * real_[bin] + imag_[bin] * imag_[bin];
    for (uint8_t band = 0; band < config::MUSIC_BAND_COUNT; ++band) {
      if (frequency >= BAND_EDGES[band] && frequency < BAND_EDGES[band + 1]) {
        energies[band] += energy;
        break;
      }
    }
  }

  float total = 0.0F;
  float onsetEnergy = 0.0F;
  float flux = 0.0F;
  for (uint8_t band = 0; band < config::MUSIC_BAND_COUNT; ++band) {
    energies[band] = log1pf(energies[band]) * 100.0F;
    total += energies[band];
    if (band < 5) {
      onsetEnergy += energies[band];
      flux += std::max(0.0F, energies[band] - previousBandEnergies_[band]);
    }
    previousBandEnergies_[band] = energies[band];
    if (noiseBands_[band] == 0.0F) noiseBands_[band] = energies[band];
    const float bandNoiseRate = energies[band] < noiseBands_[band] ? 0.08F : 0.004F;
    noiseBands_[band] += bandNoiseRate * (energies[band] - noiseBands_[band]);
    const float aboveNoise =
        std::max(0.0F, energies[band] - noiseBands_[band] * 1.08F);
    peakBands_[band] = std::max(aboveNoise, peakBands_[band] * 0.985F);
    const float normalized = peakBands_[band] > 1.0F
                                 ? 15.0F * aboveNoise / peakBands_[band]
                                 : 0.0F;
    const float rate = normalized > smoothedBands_[band]
                           ? config::MUSIC_SPECTRUM_ATTACK
                           : config::MUSIC_SPECTRUM_RELEASE;
    smoothedBands_[band] += rate * (normalized - smoothedBands_[band]);
  }

  // Starting while music is already playing must not teach its full level as
  // silence. The floor follows quieter passages quickly but rises very slowly.
  if (noiseFloor_ == 0.0F) noiseFloor_ = total * 0.35F;
  const float noiseRate = total < noiseFloor_ ? 0.08F : 0.0002F;
  noiseFloor_ += noiseRate * (total - noiseFloor_);
  if (total > std::max(config::MUSIC_MIN_ENERGY,
                       noiseFloor_ * config::MUSIC_NOISE_MULTIPLIER)) {
    lastStrongEnergyAt_ = frame.endedAt;
  }

  const bool beat = frame.endedAt - lastBeatAt_ >= config::MUSIC_BEAT_REFRACTORY_MS &&
                    total >= config::MUSIC_MIN_ENERGY &&
                    total > noiseFloor_ * config::MUSIC_NOISE_MULTIPLIER &&
                    flux > onsetEnergy * config::MUSIC_ONSET_RATIO;
  if (config::MUSIC_DEBUG &&
      frame.endedAt - lastLevelDebugAt_ >= 500) {
    lastLevelDebugAt_ = frame.endedAt;
    Serial.printf(
        "MUSIC: LEVEL total=%.0f noise=%.0f flux=%.0f onset=%.0f gate=%.0f\n",
        total, noiseFloor_, flux, onsetEnergy,
        std::max(config::MUSIC_MIN_ENERGY,
                 noiseFloor_ * config::MUSIC_NOISE_MULTIPLIER));
  }

  float strength = 0.0F;
  uint16_t bpm = 0;
  float confidence = 0.0F;
  if (beat) {
    lastBeatIntervalMs_ = lastBeatAt_ == 0 ? 0 : frame.endedAt - lastBeatAt_;
    lastBeatAt_ = frame.endedAt;
    beatTimes_[beatIndex_] = frame.endedAt;
    beatIndex_ = (beatIndex_ + 1) % config::MUSIC_REQUIRED_BEATS;
    if (beatCount_ < config::MUSIC_REQUIRED_BEATS) ++beatCount_;
    strength = std::min(1.0F, flux / std::max(1.0F, onsetEnergy));
    if (rhythmIsRegular(bpm, confidence)) {
      active_ = true;
      lastRegularAt_ = frame.endedAt;
    }
  } else if (active_) {
    rhythmIsRegular(bpm, confidence);
  }

  if (active_ &&
      (frame.endedAt - lastBeatAt_ >= config::MUSIC_STOP_TIMEOUT_MS ||
       frame.endedAt - lastStrongEnergyAt_ >= config::MUSIC_STOP_TIMEOUT_MS ||
       frame.endedAt - lastRegularAt_ >=
           config::MUSIC_REGULARITY_TIMEOUT_MS)) {
    active_ = false;
    beatCount_ = 0;
    beatIndex_ = 0;
    bpm = 0;
  }

  portENTER_CRITICAL(&lock_);
  published_.active = active_;
  published_.candidate =
      !active_ && beatCount_ >= 2 &&
      lastBeatIntervalMs_ >= config::MUSIC_BEAT_REFRACTORY_MS &&
      lastBeatIntervalMs_ <= 60000UL / config::MUSIC_MIN_BPM &&
      frame.endedAt - lastBeatAt_ < config::MUSIC_STOP_TIMEOUT_MS;
  published_.lastBeatAt = lastBeatAt_;
  published_.bpm = bpm;
  published_.confidence = confidence;
  if (beat) {
    ++published_.beatSequence;
    published_.beatStrength = strength;
  }
  for (uint8_t i = 0; i < config::MUSIC_BAND_COUNT; ++i) {
    published_.bands[i] = static_cast<uint8_t>(
        std::max(0.0F, std::min(15.0F, smoothedBands_[i])));
  }
  portEXIT_CRITICAL(&lock_);

  if (beat && config::MUSIC_DEBUG) {
    Serial.printf("MUSIC: BEAT bpm=%u strength=%.2f\n", bpm, strength);
  }
}
