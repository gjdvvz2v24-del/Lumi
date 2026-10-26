#include "display_idle.h"
#include <stdatomic.h>
/* bober_now_ms starts at boot. Audio, Wi-Fi, motion and ASR never renew it. */
static _Atomic uint32_t last_wake_ms;
bool lumi_display_visible(uint32_t now){return (uint32_t)(now-atomic_load(&last_wake_ms))<60000u;}
void lumi_display_wake(uint32_t now){atomic_store(&last_wake_ms,now);}
