#include "EdgeImpulseVoiceModel.h"

// Keep the generated SDK and every EI-specific type inside this translation
// unit. Changes to voice orchestration no longer need to parse the model SDK.
#include <burmatov.pm-project-1_inferencing.h>

#include "Config.h"

static_assert(EI_CLASSIFIER_FREQUENCY == config::AUDIO_INPUT_SAMPLE_RATE,
              "Edge Impulse and AudioCapture sample rates differ");
static_assert(EI_CLASSIFIER_RAW_SAMPLE_COUNT ==
                  config::AUDIO_INPUT_SAMPLE_RATE * config::VOICE_WINDOW_MS /
                      1000,
              "Edge Impulse model must use the configured one-second window");
static_assert(EI_CLASSIFIER_LABEL_COUNT <= edge_impulse_voice::RESULT_CAPACITY,
              "Increase Edge Impulse result capacity for this model");

namespace {

const int16_t* activeSamples = nullptr;
size_t activeSampleCount = 0;

int signalGetData(size_t offset, size_t length, float* output) {
  if (activeSamples == nullptr || offset + length > activeSampleCount) {
    return -1;
  }
  numpy::int16_to_float(activeSamples + offset, output, length);
  return 0;
}

}  // namespace

namespace edge_impulse_voice {

size_t sampleCount() { return EI_CLASSIFIER_RAW_SAMPLE_COUNT; }

uint32_t sampleRate() { return EI_CLASSIFIER_FREQUENCY; }

void begin() { run_classifier_init(); }

Result infer(const int16_t* samples, size_t count, bool rawDebug) {
  Result output;
  if (samples == nullptr || count != EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
    output.errorCode = -1;
    return output;
  }

  activeSamples = samples;
  activeSampleCount = count;
  signal_t signal = {};
  signal.total_length = count;
  signal.get_data = signalGetData;
  ei_impulse_result_t inference = {};
  const EI_IMPULSE_ERROR error =
      run_classifier(&signal, &inference, rawDebug);
  activeSamples = nullptr;
  activeSampleCount = 0;

  output.errorCode = static_cast<int>(error);
  output.success = error == EI_IMPULSE_OK;
  output.dspMs = inference.timing.dsp;
  output.classificationMs = inference.timing.classification;
  if (!output.success) return output;

  output.scoreCount = EI_CLASSIFIER_LABEL_COUNT;
  for (size_t i = 0; i < output.scoreCount; ++i) {
    output.scores[i].label = inference.classification[i].label;
    output.scores[i].value = inference.classification[i].value;
  }
  return output;
}

}  // namespace edge_impulse_voice
