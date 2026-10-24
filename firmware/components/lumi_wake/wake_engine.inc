#include "wake_detector.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#define WAKE_LOG(...) ESP_LOGI("wake", __VA_ARGS__)
#define WAKE_ERROR(...) ESP_LOGE("wake", __VA_ARGS__)
#else
#include <cstdio>
#define WAKE_LOG(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define WAKE_ERROR(...) WAKE_LOG(__VA_ARGS__)
#endif

extern const uint8_t lumi_wake_model[];
extern const size_t lumi_wake_model_size;
namespace {
constexpr size_t kArenaBytes = 192 * 1024;
constexpr size_t kVariableArenaBytes = 8 * 1024;
constexpr unsigned kAverageCount = 10;
constexpr float kThreshold = WAKE_THRESHOLD;
constexpr float kPeakThreshold = 0.80f;
// Matches pymicro-features 2.0.2, used for training and WAV validation.
constexpr float kFeatureScale = 0.0390625f;
FrontendState frontend{};
tflite::MicroMutableOpResolver<15> resolver;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr, *output = nullptr;
uint8_t *arena = nullptr, *variable_arena = nullptr;
bool frontend_ready = false, resolver_ready = false, healthy = false;
int16_t chunk[160];
int32_t dc_previous_input = 0, dc_previous_output = 0;
// Keep one second of raw audio evidence. The model alone confuses mains hum
// with the phrase. A constant/DC or low-frequency-only signal cannot wake it.
struct SignalStats { int64_t sum = 0, square = 0, difference_square = 0; };
constexpr unsigned kSignalWindows = 100;
SignalStats signal_windows[kSignalWindows]{}, signal_total{}, signal_chunk{};
unsigned signal_pos = 0, signal_count = 0;
void update_signal_evidence() {
    const auto &old = signal_windows[signal_pos];
    signal_total.sum += signal_chunk.sum - old.sum;
    signal_total.square += signal_chunk.square - old.square;
    signal_total.difference_square += signal_chunk.difference_square - old.difference_square;
    signal_windows[signal_pos] = signal_chunk;
    signal_chunk = {};
    signal_pos = (signal_pos + 1) % kSignalWindows;
    if (signal_count < kSignalWindows) ++signal_count;
}
bool has_speech_band_evidence() {
    const int64_t n = signal_count * 160;
    if (!n) return false;
    const int64_t ac_energy = signal_total.square - signal_total.sum * signal_total.sum / n;
    // At least 16 PCM counts AC RMS; first-difference/AC-energy ratio above
    // 1/400 rejects DC and 50/60/100/120 Hz hum independent of its loudness.
    return ac_energy >= n * 256 && signal_total.difference_square * 400 > ac_energy;
}
size_t chunk_size = 0, arena_used = 0;
unsigned input_frames = 0, total_frames = 0, score_count = 0, score_pos = 0;
float scores[kAverageCount]{}, score_sum = 0, last_score = 0;

void *allocate_arena(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::aligned_alloc(16, n);
#endif
}
bool prepare_resolver() {
    if (resolver_ready) return true;
    // Exactly the operators present in the exported model, including state.
    if (resolver.AddCallOnce() != kTfLiteOk || resolver.AddVarHandle() != kTfLiteOk ||
        resolver.AddReshape() != kTfLiteOk || resolver.AddReadVariable() != kTfLiteOk ||
        resolver.AddConcatenation() != kTfLiteOk || resolver.AddStridedSlice() != kTfLiteOk ||
        resolver.AddAssignVariable() != kTfLiteOk || resolver.AddConv2D() != kTfLiteOk ||
        resolver.AddDepthwiseConv2D() != kTfLiteOk || resolver.AddAdd() != kTfLiteOk ||
        resolver.AddSplitV() != kTfLiteOk || resolver.AddAveragePool2D() != kTfLiteOk ||
        resolver.AddFullyConnected() != kTfLiteOk || resolver.AddLogistic() != kTfLiteOk ||
        resolver.AddQuantize() != kTfLiteOk) return false;
    resolver_ready = true;
    return true;
}

bool process_chunk() {
    update_signal_evidence();
    size_t consumed = 0;
    FrontendOutput features = FrontendProcessSamples(&frontend, chunk, 160, &consumed);
    if (consumed != 160 || (features.size && features.size != 40)) {
        healthy = false;
        WAKE_ERROR("Unexpected frontend output: consumed=%u features=%u", (unsigned)consumed, (unsigned)features.size);
        return false;
    }
    if (!features.size) return false;
    ++total_frames;
    for (size_t i = 0; i < 40; ++i) {
        const float feature = features.values[i] * kFeatureScale;
        // nearbyint uses round-to-nearest-even, as numpy.rint in host validation.
        int q = (int)std::nearbyint(feature / input->params.scale + input->params.zero_point);
        input->data.int8[input_frames * 40 + i] = (int8_t)std::max(-128, std::min(127, q));
    }
    if (++input_frames != 3) return false;
    input_frames = 0;
    if (interpreter->Invoke() != kTfLiteOk) {
        healthy = false;
        WAKE_ERROR("TFLite Micro invocation failed; conversation stays disabled");
        return false;
    }
    float probability = (int(output->data.uint8[0]) - output->params.zero_point) * output->params.scale;
    score_sum -= scores[score_pos];
    scores[score_pos] = probability;
    score_sum += probability;
    score_pos = (score_pos + 1) % kAverageCount;
    if (score_count < kAverageCount) ++score_count;
    last_score = score_sum / score_count;
    // Ignore frontend startup transients; require a complete smoothing window.
    return total_frames >= 100 && score_count == kAverageCount && last_score > kThreshold &&
           *std::max_element(scores, scores + kAverageCount) > kPeakThreshold && has_speech_band_evidence();
}
}

extern "C" bool lumi_wake_init(void) {
    if (healthy) return true;
    lumi_wake_deinit();
    if (!prepare_resolver()) return false;
    const tflite::Model *model = tflite::GetModel(lumi_wake_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) return false;
    arena = static_cast<uint8_t *>(allocate_arena(kArenaBytes));
    variable_arena = static_cast<uint8_t *>(allocate_arena(kVariableArenaBytes));
    if (!arena || !variable_arena) { lumi_wake_deinit(); return false; }
    auto *allocator = tflite::MicroAllocator::Create(variable_arena, kVariableArenaBytes);
    if (!allocator) { lumi_wake_deinit(); return false; }
    auto *variables = tflite::MicroResourceVariables::Create(allocator, 32);
    if (!variables) { lumi_wake_deinit(); return false; }
    interpreter = new (std::nothrow) tflite::MicroInterpreter(model, resolver, arena, kArenaBytes, variables);
    if (!interpreter || interpreter->AllocateTensors() != kTfLiteOk) { lumi_wake_deinit(); return false; }
    input = interpreter->input(0);
    output = interpreter->output(0);
    if (!input || !output || input->type != kTfLiteInt8 || output->type != kTfLiteUInt8 ||
        input->dims->size != 3 || input->dims->data[0] != 1 || input->dims->data[1] != 3 ||
        input->dims->data[2] != 40 || input->bytes != 120 || output->bytes != 1 ||
        !(input->params.scale > 0) || !(output->params.scale > 0)) {
        lumi_wake_deinit(); return false;
    }
    arena_used = interpreter->arena_used_bytes();
    FrontendConfig cfg{};
    FrontendFillConfigWithDefaults(&cfg);
    cfg.window.size_ms = 30; cfg.window.step_size_ms = 10;
    cfg.filterbank.num_channels = 40;
    cfg.filterbank.lower_band_limit = 125.0f; cfg.filterbank.upper_band_limit = 7500.0f;
    cfg.noise_reduction.smoothing_bits = 10;
    cfg.noise_reduction.even_smoothing = 0.025f; cfg.noise_reduction.odd_smoothing = 0.06f;
    cfg.noise_reduction.min_signal_remaining = 0.05f;
    cfg.pcan_gain_control.enable_pcan = 1; cfg.pcan_gain_control.strength = 0.95f;
    cfg.pcan_gain_control.offset = 80.0f; cfg.pcan_gain_control.gain_bits = 21;
    cfg.log_scale.enable_log = 1; cfg.log_scale.scale_shift = 6;
    frontend_ready = true;
    if (!FrontendPopulateState(&cfg, &frontend, 16000)) { lumi_wake_deinit(); return false; }
    healthy = true;
    if (!lumi_wake_reset()) { lumi_wake_deinit(); return false; }
    WAKE_LOG("%s ready: model=%u arena_used=%u reserved=%u cutoff=%.2f peak=0.80 window=10", WAKE_LABEL, (unsigned)lumi_wake_model_size, (unsigned)arena_used, (unsigned)kArenaBytes, (double)kThreshold);
    return true;
}

extern "C" void lumi_wake_deinit(void) {
    healthy = false;
    delete interpreter; interpreter = nullptr; input = output = nullptr;
    if (frontend_ready) FrontendFreeStateContents(&frontend);
    frontend_ready = false; std::memset(&frontend, 0, sizeof(frontend));
    std::free(arena); arena = nullptr;
    std::free(variable_arena); variable_arena = nullptr;
    arena_used = 0;
}
extern "C" bool lumi_wake_reset(void) {
    if (!healthy || !interpreter || !frontend_ready) return false;
    if (interpreter->Reset() != kTfLiteOk) { healthy = false; return false; }
    FrontendReset(&frontend);
    dc_previous_input = dc_previous_output = 0;
    for (auto &window : signal_windows) window = {};
    signal_total = signal_chunk = {};
    signal_pos = signal_count = 0;
    chunk_size = input_frames = total_frames = score_count = score_pos = 0;
    score_sum = last_score = 0;
    std::memset(scores, 0, sizeof(scores));
    return true;
}
extern "C" bool lumi_wake_process(const int16_t *samples, size_t count) {
    if (!healthy || (!samples && count)) return false;
    while (count) {
        size_t take = std::min(count, 160 - chunk_size);
        // ES8311 startup/DC offsets must not become artificial spectral features.
        // 70 Hz DC blocker, also used by the conversation microphone path.
        for (size_t i = 0; i < take; ++i) {
            int32_t x = samples[i];
            int64_t difference = x - dc_previous_input;
            signal_chunk.sum += x;
            signal_chunk.square += (int64_t)x * x;
            signal_chunk.difference_square += difference * difference;
            int32_t y = x - dc_previous_input + (dc_previous_output * 31900) / 32768;
            dc_previous_input = x; dc_previous_output = y;
            chunk[chunk_size + i] = (int16_t)std::max<int32_t>(-32768, std::min<int32_t>(32767, y));
        }
        samples += take; count -= take; chunk_size += take;
        if (chunk_size == 160) {
            chunk_size = 0;
            if (process_chunk()) return true;
            if (!healthy) return false;
        }
    }
    return false;
}
extern "C" bool lumi_wake_healthy(void) { return healthy; }
extern "C" float lumi_wake_score(void) { return last_score; }
extern "C" size_t lumi_wake_arena_used(void) { return arena_used; }

extern "C" float lumi_wake_signal_rms(void) {
    int64_t n = signal_count * 160;
    if (!n) return 0;
    double variance = (double)signal_total.square / n - std::pow((double)signal_total.sum / n, 2);
    return std::sqrt(std::max(0.0, variance));
}
extern "C" float lumi_wake_signal_ratio(void) {
    int64_t n = signal_count * 160;
    if (!n) return 0;
    int64_t ac = signal_total.square - signal_total.sum * signal_total.sum / n;
    return ac > 0 ? (float)((double)signal_total.difference_square / ac) : 0;
}
extern "C" bool lumi_wake_signal_ready(void) { return has_speech_band_evidence(); }

extern "C" float lumi_wake_peak_score(void) { return *std::max_element(scores, scores + kAverageCount); }

#if defined(ESP_PLATFORM) && defined(WAKE_HEJ_SELFTEST)
extern const uint8_t selftest_start[] asm("_binary_selftest_pcm_start");
extern const uint8_t selftest_end[] asm("_binary_selftest_pcm_end");
extern "C" void lumi_wake_self_test(void) {
 for(int cycle=0;cycle<2;cycle++) {
  if(!lumi_wake_reset())return;
  bool heard=false;float peak=0;int16_t pcm[160];
  for(size_t offset=0;offset<(size_t)(selftest_end-selftest_start);offset+=sizeof(pcm)) {
   size_t bytes=std::min(sizeof(pcm),(size_t)(selftest_end-selftest_start)-offset);
   std::memcpy(pcm,selftest_start+offset,bytes);
   heard=lumi_wake_process(pcm,bytes/2)||heard;
   peak=std::max(peak,lumi_wake_score());
  }
  WAKE_LOG("WAKE_SELF_TEST cycle=%d heard=%d peak=%.3f healthy=%d",cycle,heard,(double)peak,healthy);
 }
 lumi_wake_reset();
}
#endif
