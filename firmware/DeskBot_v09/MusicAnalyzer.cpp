#include "MusicAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <esp_timer.h>

static_assert(config::MUSIC_FFT_SIZE >= 64 && config::MUSIC_FFT_SIZE <= 1024 &&
                  (config::MUSIC_FFT_SIZE & (config::MUSIC_FFT_SIZE - 1)) == 0,
              "MUSIC_FFT_SIZE must be a power of two from 64 to 1024");
static_assert(config::MUSIC_SAMPLE_RATE >= 8000,
              "Music spectrum requires at least 8 kHz sampling");
static_assert(config::AUDIO_INPUT_SAMPLE_RATE / 2 ==
                  config::MUSIC_SAMPLE_RATE,
              "MusicAnalyzer decimator expects 16 kHz -> 8 kHz");
static_assert(config::MUSIC_SAMPLE_RATE / 2 >= 4000,
              "Sample rate must cover the highest equalizer band");
static_assert(config::MUSIC_MIN_BPM > 0 &&
                  config::MUSIC_MIN_BPM < config::MUSIC_MAX_BPM,
              "Invalid music BPM range");
static_assert(config::MUSIC_BAND_COUNT == 8,
              "The OLED equalizer layout requires eight bands");
static_assert(config::MUSIC_REQUIRED_BEATS >= 6,
              "Music confirmation needs at least six beats");

namespace {
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

bool MusicAnalyzer::begin(AudioCapture& capture) {
  capture_ = &capture;
  if (!capture.ready()) return false;
  for (size_t i = 0; i < config::MUSIC_FFT_SIZE; ++i) {
    hannWindow_[i] = 0.5F - 0.5F * cosf(
        2.0F * PI_F * i / (config::MUSIC_FFT_SIZE - 1));
  }
  memset(binBands_, 0xFF, sizeof(binBands_));
  for (size_t bin = 1; bin <= config::MUSIC_FFT_SIZE / 2; ++bin) {
    const float frequency = static_cast<float>(bin) * config::MUSIC_SAMPLE_RATE /
                            config::MUSIC_FFT_SIZE;
    for (uint8_t band = 0; band < config::MUSIC_BAND_COUNT; ++band) {
      if (frequency >= BAND_EDGES[band] && frequency < BAND_EDGES[band + 1]) {
        binBands_[bin] = band;
        break;
      }
    }
  }
  return xTaskCreatePinnedToCore(
             taskEntry, "deskbot-music", config::MUSIC_TASK_STACK_WORDS,
             this, config::MUSIC_TASK_PRIORITY, &taskHandle_,
             config::MUSIC_TASK_CORE) == pdPASS;
}

void MusicAnalyzer::setSuppressed(bool suppressed) {
  portENTER_CRITICAL(&lock_);
  const bool changed = suppressed_ != suppressed;
  suppressed_ = suppressed;
  if (changed) resetRequested_ = true;
  portEXIT_CRITICAL(&lock_);
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
    if (!capture_->receiveMusicFrame(frame, portMAX_DELAY)) continue;
    portENTER_CRITICAL(&lock_);
    const bool accept = !suppressed_;
    const bool reset = resetRequested_;
    resetRequested_ = false;
    portEXIT_CRITICAL(&lock_);
    if (reset) resetDetector();
    if (!accept) {
      continue;
    }
    const uint32_t gap = previousSequence_ != 0 && frame.sequence > previousSequence_
                             ? frame.sequence - previousSequence_ - 1
                             : 0;
    previousSequence_ = frame.sequence;
    if (gap != 0) {
      portENTER_CRITICAL(&lock_);
      published_.droppedFrames += gap;
      portEXIT_CRITICAL(&lock_);
      // Never join FIR or FFT data from opposite sides of a discontinuity.
      resetDetector();
    }
    const int64_t startedAtUs = esp_timer_get_time();
    consumeInput(frame);
    const uint32_t elapsedUs =
        static_cast<uint32_t>(esp_timer_get_time() - startedAtUs);
    portENTER_CRITICAL(&lock_);
    ++published_.inputFrames;
    published_.busyTimeUs += elapsedUs;
    published_.taskCore = xPortGetCoreID();
    published_.stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
    portEXIT_CRITICAL(&lock_);

    // xQueueReceive() does not block while queued frames are available. Enter
    // Blocked state explicitly so IDLE0 always gets a chance to feed its WDT.
    vTaskDelay(pdMS_TO_TICKS(config::MUSIC_POST_FRAME_YIELD_MS));
  }
}

void MusicAnalyzer::consumeInput(const AudioFrame& frame) {
  // Seven-tap half-band FIR, cutoff at 4 kHz for 16 kHz -> 8 kHz decimation.
  constexpr float taps[7] = {
      -0.0456359F, 0.0F, 0.2956359F, 0.5F,
       0.2956359F, 0.0F, -0.0456359F};
  for (int16_t sample : frame.samples) {
    firHistory_[firIndex_] = sample;
    firIndex_ = (firIndex_ + 1) % 7;
    decimationPhase_ = !decimationPhase_;
    if (!decimationPhase_) continue;
    float filtered = 0.0F;
    for (uint8_t tap = 0; tap < 7; ++tap) {
      const uint8_t index = (firIndex_ + tap) % 7;
      filtered += firHistory_[index] * taps[tap];
    }
    filtered = std::max(-32768.0F, std::min(32767.0F, filtered));
    analysisFrame_.samples[analysisFrameIndex_++] =
        static_cast<int16_t>(filtered);
    if (analysisFrameIndex_ == config::MUSIC_FFT_SIZE) {
      analysisFrame_.endedAt = frame.endedAt;
      analyze(analysisFrame_);
      analysisFrameIndex_ = 0;
    }
  }
}

void MusicAnalyzer::resetDetector() {
  noiseFloor_ = 0.0F;
  lastBeatAt_ = 0;
  lastBeatIntervalMs_ = 0;
  lastSignalAt_ = 0;
  lastMusicEvidenceAt_ = 0;
  regularWindowStartedAt_ = 0;
  regularWindowBpm_ = 0;
  regularWindowCount_ = 0;
  beatCount_ = 0;
  beatIndex_ = 0;
  active_ = false;
  analysisFrameIndex_ = 0;
  firIndex_ = 0;
  decimationPhase_ = false;
  memset(firHistory_, 0, sizeof(firHistory_));
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
  confidence = std::max(
      0.0F, std::min(1.0F, 1.0F - deviation / intervalCount));
  const unsigned long sequenceSpan =
      ordered[config::MUSIC_REQUIRED_BEATS - 1] - ordered[0];
  return close >= 4 &&
         sequenceSpan >= config::MUSIC_MIN_CONFIRM_SPAN_MS &&
         sequenceSpan <= 5200;
}

void MusicAnalyzer::analyze(const AnalysisFrame& frame) {
  const int64_t startedAtUs = esp_timer_get_time();
  float mean = 0.0F;
  for (size_t i = 0; i < config::MUSIC_FFT_SIZE; ++i) mean += frame.samples[i];
  mean /= config::MUSIC_FFT_SIZE;
  double sumSquares = 0.0;
  for (size_t i = 0; i < config::MUSIC_FFT_SIZE; ++i) {
    const float centered = static_cast<float>(frame.samples[i]) - mean;
    sumSquares += static_cast<double>(centered) * centered;
    real_[i] = centered * hannWindow_[i];
    imag_[i] = 0.0F;
  }
  const float signalRms = sqrtf(static_cast<float>(
      sumSquares / config::MUSIC_FFT_SIZE));
  const bool signalPresent = signalRms >= config::MUSIC_MIN_SIGNAL_RMS;
  if (signalPresent) lastSignalAt_ = frame.endedAt;
  fft(real_, imag_, config::MUSIC_FFT_SIZE);

  float energies[config::MUSIC_BAND_COUNT] = {};
  for (size_t bin = 1; bin <= config::MUSIC_FFT_SIZE / 2; ++bin) {
    const float energy = real_[bin] * real_[bin] + imag_[bin] * imag_[bin];
    const uint8_t band = binBands_[bin];
    if (band < config::MUSIC_BAND_COUNT) energies[band] += energy;
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
  const bool beat = frame.endedAt - lastBeatAt_ >= config::MUSIC_BEAT_REFRACTORY_MS &&
                    signalPresent &&
                    total >= config::MUSIC_MIN_ENERGY &&
                    total > noiseFloor_ * config::MUSIC_NOISE_MULTIPLIER &&
                    flux > onsetEnergy * config::MUSIC_ONSET_RATIO;
  float strength = 0.0F;
  uint16_t bpm = 0;
  float confidence = 0.0F;
  bool regular = false;
  if (!active_ && regularWindowCount_ != 0 &&
      frame.endedAt - regularWindowStartedAt_ >
          config::MUSIC_START_CONFIRM_MS) {
    regularWindowStartedAt_ = 0;
    regularWindowBpm_ = 0;
    regularWindowCount_ = 0;
  }
  if (beat) {
    lastBeatIntervalMs_ = lastBeatAt_ == 0 ? 0 : frame.endedAt - lastBeatAt_;
    lastBeatAt_ = frame.endedAt;
    beatTimes_[beatIndex_] = frame.endedAt;
    beatIndex_ = (beatIndex_ + 1) % config::MUSIC_REQUIRED_BEATS;
    if (beatCount_ < config::MUSIC_REQUIRED_BEATS) ++beatCount_;
    strength = std::min(1.0F, flux / std::max(1.0F, onsetEnergy));
    regular = rhythmIsRegular(bpm, confidence);
    if (!active_) {
      // `regular` is deliberately stricter than start confirmation and remains
      // useful diagnostics. Real music with extra onsets can have a stable BPM
      // and high confidence while that strict sliding-window flag is false.
      const bool startQuality =
          bpm != 0 && confidence >= config::MUSIC_START_CONFIDENCE &&
          signalRms >= config::MUSIC_START_MIN_SIGNAL_RMS;
      if (startQuality) {
        const float bpmDifference = regularWindowBpm_ == 0
                                        ? 0.0F
                                        : fabsf(static_cast<float>(bpm) -
                                                regularWindowBpm_) /
                                              regularWindowBpm_;
        if (regularWindowCount_ == 0 ||
            bpmDifference > config::MUSIC_START_BPM_TOLERANCE) {
          regularWindowStartedAt_ = frame.endedAt;
          regularWindowBpm_ = bpm;
          regularWindowCount_ = 1;
        } else if (regularWindowCount_ <
                   config::MUSIC_START_REGULAR_WINDOWS) {
          ++regularWindowCount_;
        }
        const bool confirmationSpanReady =
            frame.endedAt - regularWindowStartedAt_ >=
            config::MUSIC_START_MIN_SPAN_MS;
        if (regularWindowCount_ >= config::MUSIC_START_REGULAR_WINDOWS &&
            confirmationSpanReady) {
          active_ = true;
          lastMusicEvidenceAt_ = frame.endedAt;
        }
      }
    }
  } else if (active_) {
    regular = rhythmIsRegular(bpm, confidence);
  }

  // Once MUSIC is active, ordinary speech or room noise may keep the RMS gate
  // open. Only a beat backed by a stable rhythm window extends the musical
  // state. This lets playback stop promptly without treating speech as
  // silence and without changing the stricter MUSIC start confirmation.
  const bool musicEvidence =
      active_ && beat && bpm != 0 &&
      confidence >= config::MUSIC_START_CONFIDENCE &&
      signalRms >= config::MUSIC_START_MIN_SIGNAL_RMS;
  if (musicEvidence) lastMusicEvidenceAt_ = frame.endedAt;

  const bool signalMissing =
      frame.endedAt - lastSignalAt_ >= config::MUSIC_STOP_TIMEOUT_MS;
  const bool rhythmMissing =
      active_ && lastMusicEvidenceAt_ != 0 &&
      frame.endedAt - lastMusicEvidenceAt_ >=
          config::MUSIC_RHYTHM_STOP_TIMEOUT_MS;
  if (active_ && (signalMissing || rhythmMissing)) {
    active_ = false;
    lastMusicEvidenceAt_ = 0;
    beatCount_ = 0;
    beatIndex_ = 0;
    regularWindowStartedAt_ = 0;
    regularWindowBpm_ = 0;
    regularWindowCount_ = 0;
    bpm = 0;
  }

  portENTER_CRITICAL(&lock_);
  const uint32_t elapsedUs =
      static_cast<uint32_t>(esp_timer_get_time() - startedAtUs);
  ++published_.fftFrames;
  published_.maxFftUs = max(published_.maxFftUs, elapsedUs);
  published_.active = active_;
  published_.candidate = !active_ && regularWindowCount_ != 0;
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
    Serial.printf(
        "MUSIC: BEAT bpm=%u strength=%.2f confidence=%.2f regular=%u confirm=%u/%u rms=%.0f total=%.0f noise=%.0f flux=%.0f\n",
        bpm, strength, confidence, regular ? 1U : 0U,
        regularWindowCount_, config::MUSIC_START_REGULAR_WINDOWS, signalRms,
        total, noiseFloor_, flux);
  }
}
