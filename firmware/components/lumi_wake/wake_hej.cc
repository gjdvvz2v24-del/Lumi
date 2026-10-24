// Instantiate the shared engine with independent frontend and model state.
#define lumi_wake_init hej_wake_init
#define lumi_wake_self_test hej_wake_self_test
#define lumi_wake_deinit hej_wake_deinit
#define lumi_wake_reset hej_wake_reset
#define lumi_wake_process hej_wake_process
#define lumi_wake_healthy hej_wake_healthy
#define lumi_wake_score hej_wake_score
#define lumi_wake_peak_score hej_wake_peak_score
#define lumi_wake_arena_used hej_wake_arena_used
#define lumi_wake_signal_rms hej_wake_signal_rms
#define lumi_wake_signal_ratio hej_wake_signal_ratio
#define lumi_wake_signal_ready hej_wake_signal_ready
#define WAKE_THRESHOLD 0.55f
#define WAKE_LABEL "Hej Lumi"
#define WAKE_HEJ_SELFTEST 1
#include "wake_engine.inc"
