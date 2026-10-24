#include "bober.h"
#include "voice_profile.h"
#include "wake_capture.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
static const char *TAG="BOBER";
bober_config_t g_cfg;
volatile bool g_wifi_ready=false, g_session_enabled=false, g_ws_ready=false, g_ws_reconnect=false;
volatile bool g_display_ready=false;
volatile uint8_t g_mic_level=0,g_speaker_level=0;
volatile uint32_t g_generation=1;
volatile bober_phase_t g_phase=BOBER_OFF;
volatile bool g_response_complete=false;
volatile int64_t g_last_pcm_ms=0, g_last_tts_begin_ms=0, g_last_play_ms=0;
StreamBufferHandle_t g_uplink=NULL;
SemaphoreHandle_t g_pcm_mutex=NULL;
uint8_t *g_pcm_ring=NULL;
size_t g_pcm_head=0,g_pcm_tail=0,g_pcm_count=0;
volatile uint32_t g_pcm_overruns=0;
int64_t bober_now_ms(void){return esp_timer_get_time()/1000;}
static void get(nvs_handle_t n,const char *key,char *dst,size_t sz){size_t x=sz;if(nvs_get_str(n,key,dst,&x)!=ESP_OK)dst[0]=0;}
static void load(void){
 memset(&g_cfg,0,sizeof(g_cfg));
 strlcpy(g_cfg.voice_id,BOBER_DEFAULT_VOICE,sizeof(g_cfg.voice_id));
 nvs_handle_t n; if(nvs_open("bober",NVS_READONLY,&n)!=ESP_OK)return;
 get(n,"ssid",g_cfg.ssid,sizeof(g_cfg.ssid));get(n,"pass",g_cfg.password,sizeof(g_cfg.password));
 get(n,"key",g_cfg.api_key,sizeof(g_cfg.api_key));
 get(n,"openai",g_cfg.openai_key,sizeof(g_cfg.openai_key));
 get(n,"voice",g_cfg.voice_id,sizeof(g_cfg.voice_id));
 // Migrate the previous Bober default while retaining explicit custom voices.
 if(!g_cfg.voice_id[0]||!strcmp(g_cfg.voice_id,"gNNJUEmXjdMOywK9rhxY"))
  strlcpy(g_cfg.voice_id,BOBER_DEFAULT_VOICE,sizeof(g_cfg.voice_id));
 nvs_close(n);
}
void bober_pcm_reset(void){if(!g_pcm_mutex)return;if(xSemaphoreTake(g_pcm_mutex,pdMS_TO_TICKS(200))==pdTRUE){g_pcm_head=g_pcm_tail=g_pcm_count=0;xSemaphoreGive(g_pcm_mutex);}}
bool bober_pcm_push(const uint8_t *data,size_t len,uint32_t generation){
 if(!g_pcm_ring||!len||len>BOBER_PCM_CAPACITY)return false;
 if(xSemaphoreTake(g_pcm_mutex,pdMS_TO_TICKS(5))!=pdTRUE)return false;
 bool ok=g_generation==generation&&g_session_enabled&&len<=BOBER_PCM_CAPACITY-g_pcm_count;
 if(ok){size_t first=len;size_t room=BOBER_PCM_CAPACITY-g_pcm_head;if(first>room)first=room;
 memcpy(g_pcm_ring+g_pcm_head,data,first);if(len>first)memcpy(g_pcm_ring,data+first,len-first);
 g_pcm_head=(g_pcm_head+len)%BOBER_PCM_CAPACITY;g_pcm_count+=len;}
 else if(g_generation==generation)g_pcm_overruns++;
 xSemaphoreGive(g_pcm_mutex);return ok;
}
size_t bober_pcm_pop(uint8_t *out,size_t len){if(xSemaphoreTake(g_pcm_mutex,pdMS_TO_TICKS(10))!=pdTRUE)return 0;
 if (len > g_pcm_count) {
  len = g_pcm_count;
 }
 len &= ~(size_t)1;
 size_t first = len;
 size_t room = BOBER_PCM_CAPACITY - g_pcm_tail;
 if (first > room) {
  first = room;
 }
 if (first) {
  memcpy(out, g_pcm_ring + g_pcm_tail, first);
 }
 if(len>first)memcpy(out+first,g_pcm_ring,len-first);
 g_pcm_tail=(g_pcm_tail+len)%BOBER_PCM_CAPACITY;g_pcm_count-=len;xSemaphoreGive(g_pcm_mutex);return len;}
size_t bober_pcm_available(void){size_t n=0;if(xSemaphoreTake(g_pcm_mutex,pdMS_TO_TICKS(10))==pdTRUE){n=g_pcm_count;xSemaphoreGive(g_pcm_mutex);}return n;}
void app_main(void){
 (void)bober_power_woke();
 if(!bober_power_init()){ESP_LOGE(TAG,"Power coordinator allocation failed");return;}
 esp_err_t e=nvs_flash_init();if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());e=nvs_flash_init();}ESP_ERROR_CHECK(e);
 load();ESP_LOGI(TAG,"Lumi OpenAI v2.9.3-singing / Hej Lumi + Luuu-miiii local wake / 115200");
 ESP_LOGI(TAG,"PSRAM free=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
 g_pcm_mutex=xSemaphoreCreateMutex();g_uplink=xStreamBufferCreate(16384,1);
 g_pcm_ring=heap_caps_malloc(BOBER_PCM_CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!g_pcm_mutex||!g_uplink||!g_pcm_ring){ESP_LOGE(TAG,"PSRAM/queues allocation failed; check CONFIG_SPIRAM");return;}
 g_phase=BOBER_WAKE_WAIT;
 // No OpenAI session or greeting until the local wake word is detected.
 xTaskCreatePinnedToCore(bober_screen_task,"screen",6144,NULL,3,NULL,1);
 if(!bober_audio_init()){ESP_LOGE(TAG,"ES8311 not ready; check Atomic Voice Base connection");return;}
 xTaskCreatePinnedToCore(bober_mic_task,"mic",6144,NULL,9,NULL,1);
 xTaskCreatePinnedToCore(bober_speaker_task,"speaker",6144,NULL,9,NULL,1);
 if(!bober_wifi_start())ESP_LOGW(TAG,"No Wi-Fi config: run configure_usb.py via USB");
 xTaskCreatePinnedToCore(bober_uplink_task,"uplink",6144,NULL,7,NULL,0);
 xTaskCreatePinnedToCore(bober_eleven_task,"openai",16384,NULL,6,NULL,0);
 xTaskCreate(lumi_capture_command_task,"capture_command",3072,NULL,2,NULL);
 xTaskCreatePinnedToCore(bober_button_task,"button",4096,NULL,6,NULL,1);
}
