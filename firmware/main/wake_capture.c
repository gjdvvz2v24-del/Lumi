/* Temporary USB-only diagnostic recording. Starts only on explicit host command.
   Recording and download never start a network session. Buffer kept until RELEASE. */
#include "bober.h"
#include "wake_capture.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include "wifi_profiles.h"
/* Atomic Wi-Fi-only replacement. The active driver reads it on next boot. */
static void set_wifi_profiles(const char *json){
 lumi_wifi_profile_t parsed[LUMI_WIFI_NETWORKS];
 if(g_session_enabled||g_phase!=BOBER_WAKE_WAIT||!lumi_wifi_profiles_parse(json,parsed)){
  memset(parsed,0,sizeof(parsed));printf("LUMI_WIFI_REJECTED\n");fflush(stdout);return;
 }
 memset(parsed,0,sizeof(parsed));
 nvs_handle_t n;esp_err_t e=nvs_open("bober",NVS_READWRITE,&n);
 if(e==ESP_OK){e=nvs_set_str(n,"wifi_profiles",json);if(e==ESP_OK)e=nvs_commit(n);nvs_close(n);}
 printf(e==ESP_OK?"LUMI_WIFI_SAVED_RESTART_REQUIRED\n":"LUMI_WIFI_FAILED\n");fflush(stdout);
}
#ifdef LUMI_SOL_ULTRAFAST
/* USB provisioning changes only this experimental key; never prints it. */
static void set_sol_key(const char *key){
 size_t len=strlen(key);
 if(g_session_enabled||len<20||len>=sizeof(g_cfg.sol_key)||strncmp(key,"sk-",3)){
  printf("LUMI_SOL_KEY_REJECTED\n");return;
 }
 for(size_t i=0;i<len;i++)if((unsigned char)key[i]<33||(unsigned char)key[i]>126){
  printf("LUMI_SOL_KEY_REJECTED\n");return;
 }
 nvs_handle_t n;esp_err_t e=nvs_open("bober",NVS_READWRITE,&n);
 if(e==ESP_OK){e=nvs_set_str(n,"sol",key);if(e==ESP_OK)e=nvs_commit(n);nvs_close(n);}
 if(e==ESP_OK)strlcpy(g_cfg.sol_key,key,sizeof(g_cfg.sol_key));
 printf(e==ESP_OK?"LUMI_SOL_KEY_SAVED\n":"LUMI_SOL_KEY_FAILED\n");fflush(stdout);
}
#endif
#define CAPTURE_SAMPLES (16000u*20u)
#define USB_COMMAND_BYTES (LUMI_WIFI_JSON_MAX+32)
static atomic_bool requested,sending,ready;
static int16_t *recording;
static size_t recorded;
static void dump_task(void *arg){
 (void)arg;
 printf("LUMI_PCM_BEGIN:%u\n",(unsigned)(recorded*2));
 unsigned char encoded[1370];
 const uint8_t *bytes=(const uint8_t *)recording;
 for(size_t offset=0;offset<recorded*2;offset+=1024){
  size_t size=recorded*2-offset;if(size>1024)size=1024;
  size_t n=0;
  if(mbedtls_base64_encode(encoded,sizeof(encoded)-1,&n,bytes+offset,size)!=0)break;
  encoded[n]=0;
  printf("LUMI_PCM:%u:%s:END\n",(unsigned)offset,encoded);
  vTaskDelay(pdMS_TO_TICKS(2));
 }
 printf("LUMI_PCM_END:%u\n",(unsigned)(recorded*2));fflush(stdout);
 atomic_store(&sending,false);vTaskDelete(NULL);
}
bool lumi_capture_feed(const int16_t *pcm,size_t count){
 if(atomic_exchange(&requested,false)){
  if(recording){printf("LUMI_CAPTURE_BUSY\n");return true;}
  recording=heap_caps_malloc(CAPTURE_SAMPLES*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!recording){printf("LUMI_CAPTURE_ALLOCATION_FAILED\n");return false;}
  recorded=0;atomic_store(&ready,false);
  printf("LUMI_CAPTURE_STARTED:20s\n");fflush(stdout);
 }
 if(!recording)return false;
 if(!atomic_load(&ready)){
  size_t n=CAPTURE_SAMPLES-recorded;if(n>count)n=count;
  memcpy(recording+recorded,pcm,n*sizeof(int16_t));recorded+=n;
  if(recorded==CAPTURE_SAMPLES){
   g_phase=BOBER_OFF;atomic_store(&ready,true);
   printf("LUMI_CAPTURE_READY:%u\n",(unsigned)(recorded*2));fflush(stdout);
  }
 }
 return true;
}
void lumi_capture_command_task(void *arg){
 (void)arg;
 char *command=heap_caps_calloc(1,USB_COMMAND_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!command){printf("LUMI_USB_COMMAND_ALLOCATION_FAILED\n");fflush(stdout);vTaskDelete(NULL);return;}
 size_t n=0;bool overflow=false;
 printf("LUMI_CAPTURE_COMMAND_READY\n");fflush(stdout);
 for(;;){
  int c=getchar();
  if(c==EOF){clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(20));continue;}
  if(c!='\n'&&c!='\r'){if(!overflow&&n+1<USB_COMMAND_BYTES)command[n++]=(char)c;else overflow=true;continue;}
  if(overflow){memset(command,0,USB_COMMAND_BYTES);n=0;overflow=false;continue;}
  command[n]=0;n=0;
  if(!strcmp(command,"LUMI_WIFI_READY?")){printf(!g_session_enabled&&g_phase==BOBER_WAKE_WAIT?"LUMI_WIFI_READY\n":"LUMI_WIFI_BUSY\n");fflush(stdout);continue;}
  if(!strncmp(command,"LUMI_WIFI_SET ",14)){set_wifi_profiles(command+14);memset(command,0,USB_COMMAND_BYTES);continue;}
#ifdef LUMI_SOL_ULTRAFAST
  if(!strncmp(command,"LUMI_SOL_KEY ",13)){set_sol_key(command+13);memset(command,0,USB_COMMAND_BYTES);continue;}
#endif
  if(!strcmp(command,"LUMI_CAPTURE_START")&&!g_session_enabled&&g_phase==BOBER_WAKE_WAIT&&!recording)atomic_store(&requested,true);
  else if(!strcmp(command,"LUMI_CAPTURE_GET")&&atomic_load(&ready)&&!atomic_exchange(&sending,true)){
   if(xTaskCreate(dump_task,"capture_dump",4096,NULL,2,NULL)!=pdPASS)atomic_store(&sending,false);
  }else if(!strcmp(command,"LUMI_CAPTURE_RELEASE")&&atomic_load(&ready)&&!atomic_load(&sending)){
   free(recording);recording=NULL;recorded=0;atomic_store(&ready,false);g_phase=BOBER_WAKE_WAIT;
   printf("LUMI_CAPTURE_RELEASED\n");fflush(stdout);
  }
 }
}
