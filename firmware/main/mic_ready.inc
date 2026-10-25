#include "mic_ready_sound.h"
/* Keep uplink muted until the local cue and the normal DMA/echo tail drain.
   The speaker owner plays this through the same PCM FIFO as speech. */
static bool mic_ready_cue_active;
static void begin_listening(void){
 if(!current(ws_generation))return;
 g_phase=BOBER_SPEAKING;g_response_complete=false;
 g_last_tts_begin_ms=bober_now_ms();
 if(bober_pcm_push((const uint8_t*)lumi_mic_ready_pcm,sizeof(lumi_mic_ready_pcm),ws_generation)){
  mic_ready_cue_active=true;g_last_pcm_ms=bober_now_ms();g_response_complete=true;
  ESP_LOGI(TAG,"Microphone ready cue queued");
 }else{
  /* An optional cue must never strand an otherwise healthy conversation. */
  mic_ready_cue_active=false;g_phase=BOBER_LISTENING;
  ESP_LOGW(TAG,"Microphone ready cue skipped: PCM unavailable");
 }
}
