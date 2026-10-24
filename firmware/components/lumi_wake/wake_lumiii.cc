// Instantiate the shared engine with independent frontend and model state.
#define lumi_wake_init lumiii_wake_init
#define lumi_wake_self_test lumiii_wake_self_test
#define lumi_wake_deinit lumiii_wake_deinit
#define lumi_wake_reset lumiii_wake_reset
#define lumi_wake_process lumiii_wake_process
#define lumi_wake_healthy lumiii_wake_healthy
#define lumi_wake_score lumiii_wake_score
#define lumi_wake_peak_score lumiii_wake_peak_score
#define lumi_wake_arena_used lumiii_wake_arena_used
#define lumi_wake_signal_rms lumiii_wake_signal_rms
#define lumi_wake_signal_ratio lumiii_wake_signal_ratio
#define lumi_wake_signal_ready lumiii_wake_signal_ready
#define lumi_wake_model lumiii_wake_model
#define lumi_wake_model_size lumiii_wake_model_size
#define WAKE_THRESHOLD 0.95f
#define WAKE_LABEL "Luuu-miiii"
#include "wake_engine.inc"
