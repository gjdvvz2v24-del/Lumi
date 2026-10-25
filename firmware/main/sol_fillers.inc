/* Called by the network owner. A single temporary worker owns Sol HTTP while
   this task can feed cached PCM. No additional network/TTS call for a filler. */
#include "filler_policy.h"
#include "esp_random.h"
#define FILLER_ASSET(n) \
 extern const uint8_t filler_##n##_start[] __asm__("_binary_filler_" #n "_pcm_start"); \
 extern const uint8_t filler_##n##_end[] __asm__("_binary_filler_" #n "_pcm_end");
FILLER_ASSET(0)
FILLER_ASSET(1)
FILLER_ASSET(2)
FILLER_ASSET(3)
FILLER_ASSET(4)
FILLER_ASSET(5)
typedef struct {reply_t *reply;atomic_bool done;bool ok;} sol_job_t;
static void sol_worker(void *arg){
 sol_job_t *job=arg;
 job->ok=sol_generate(job->reply);
 atomic_store_explicit(&job->done,true,memory_order_release);
 vTaskDelete(NULL);
}
static bool sol_generate_with_filler(reply_t *r){
 static int64_t last_filler_ms;
 static unsigned last_index=6;
 int64_t start=bober_now_ms();
 bool eligible=lumi_filler_eligible(r->text,start,last_filler_ms,esp_random());
 sol_job_t job={.reply=r,.done=ATOMIC_VAR_INIT(false),.ok=false};
 if(xTaskCreate(sol_worker,"sol_request",8192,&job,5,NULL)!=pdPASS)return sol_generate(r);
 bool played=false;
 while(!atomic_load_explicit(&job.done,memory_order_acquire)){
  if(!played&&eligible&&current(r->generation)&&!g_ws_reconnect&&bober_now_ms()-start>=1200){
   /* Once started, finish this short clip before queuing the real answer. */
   played=true;unsigned index=esp_random()%6;if(index==last_index)index=(index+1)%6;
   const uint8_t *begins[]={filler_0_start,filler_1_start,filler_2_start,filler_3_start,filler_4_start,filler_5_start};
   const uint8_t *ends[]={filler_0_end,filler_1_end,filler_2_end,filler_3_end,filler_4_end,filler_5_end};
   const uint8_t *p=begins[index];
   last_index=index;last_filler_ms=bober_now_ms();
   while(p<ends[index]&&current(r->generation)&&!g_ws_reconnect){
    size_t n=(size_t)(ends[index]-p);if(n>2048)n=2048;
    if(bober_pcm_push(p,n,r->generation)){p+=n;g_last_pcm_ms=bober_now_ms();}
    else vTaskDelay(pdMS_TO_TICKS(10));
   }
   ESP_LOGI(TAG,"Cached thinking filler index=%u",index);
  }
  vTaskDelay(pdMS_TO_TICKS(20));
 }
 /* Always join the worker, including cancellation, before releasing its job. */
 return job.ok;
}
