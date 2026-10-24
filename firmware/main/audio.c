#include "bober.h"
#include "wake_detector.h"
#include "wake_capture.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_check.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
static const char *TAG="audio";
static i2s_chan_handle_t rx=NULL,tx=NULL;
static i2c_master_bus_handle_t ext_bus=NULL;
static i2c_master_dev_handle_t amp=NULL;
static esp_codec_dev_handle_t codec_dev=NULL;
static int32_t dc_prev_x=0,dc_prev_y=0;
/* IMU only requests a wake. The microphone owner releases both models before
   enabling TLS, just as for an acoustic wake; simultaneous triggers coalesce. */
static atomic_bool shake_wake_requested;
void bober_request_shake_wake(void){
 if(!g_session_enabled&&g_phase==BOBER_WAKE_WAIT&&!bober_power_requested())
  atomic_store(&shake_wake_requested,true);
}
// External Atomic Voice Base: codec 0x18, expander 0x43, SDA=38 SCL=39.
static esp_err_t ext_write(uint8_t reg,uint8_t value){
 if(!amp)return ESP_ERR_INVALID_STATE;
 uint8_t tmp[2]={reg,value};return i2c_master_transmit(amp,tmp,2,100);
}
static void set_amp(bool on){if(!amp)return;ext_write(0x05,on?0xff:0xfe);}
bool bober_audio_init(void){
 // ES8311 A149 uses BCLK as clock source; no dedicated MCLK pad.
 // 32-bit stereo bus preserves the SCK rate used by the M5Stack reference.
 i2s_chan_config_t chan=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
 chan.auto_clear=true;
 ESP_ERROR_CHECK(i2s_new_channel(&chan,&tx,&rx));
 i2s_std_config_t std={
  .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(BOBER_RATE),
  .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
  .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=GPIO_NUM_8,.ws=GPIO_NUM_6,.dout=GPIO_NUM_5,.din=GPIO_NUM_7},
 };
 ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx,&std));
 ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx,&std));
 // TX before RX: shared duplex clock.
 ESP_ERROR_CHECK(i2s_channel_enable(tx));
 ESP_ERROR_CHECK(i2s_channel_enable(rx));
 i2c_master_bus_config_t bus={.i2c_port=I2C_NUM_0,.sda_io_num=GPIO_NUM_38,.scl_io_num=GPIO_NUM_39,
  .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
 ESP_ERROR_CHECK(i2c_new_master_bus(&bus,&ext_bus));
 audio_codec_i2c_cfg_t ic={.port=I2C_NUM_0,.addr=ES8311_CODEC_DEFAULT_ADDR,.bus_handle=ext_bus};
 const audio_codec_ctrl_if_t *ctrl=audio_codec_new_i2c_ctrl(&ic);
 audio_codec_i2s_cfg_t dc={.port=I2S_NUM_0,.rx_handle=rx,.tx_handle=tx};
 const audio_codec_data_if_t *data=audio_codec_new_i2s_data(&dc);
 const audio_codec_gpio_if_t *gpio=audio_codec_new_gpio();
 if(!ctrl||!data||!gpio){ESP_LOGE(TAG,"Could not initialize ES8311 codec interfaces");return false;}
 es8311_codec_cfg_t codec={.ctrl_if=ctrl,.gpio_if=gpio,.codec_mode=ESP_CODEC_DEV_WORK_MODE_BOTH,
  .master_mode=false,.use_mclk=false,.digital_mic=false,.pa_pin=-1,
  .hw_gain={.pa_voltage=5.0,.codec_dac_voltage=3.3},.mclk_div=256};
 const audio_codec_if_t *iface=es8311_codec_new(&codec);
 if(!iface){ESP_LOGE(TAG,"ES8311 device unavailable");return false;}
 esp_codec_dev_cfg_t cfg={.dev_type=ESP_CODEC_DEV_TYPE_IN_OUT,.codec_if=iface,.data_if=data};
 esp_codec_dev_handle_t dev=esp_codec_dev_new(&cfg);
 if(!dev)return false;
 codec_dev=dev;
 esp_codec_dev_sample_info_t format={.bits_per_sample=32,.channel=2,.channel_mask=3,
  .sample_rate=BOBER_RATE,.mclk_multiple=256};
 if(esp_codec_dev_open(dev,&format)!=ESP_CODEC_DEV_OK){ESP_LOGE(TAG,"ES8311 open failed");return false;}
 esp_codec_dev_set_out_vol(dev,90);
 // ES8311 starts with a very low post-PGA digital gain (v1.5: 6 dB).
 // 24 dB is a moderate starting point: +18 dB vs v1.5, not the clipped 42 dB maximum.
 // Analog PGA is configured by the codec driver separately.
 int mic_gain_status=esp_codec_dev_set_in_gain(dev,24.0f);
 ESP_LOGI(TAG,"ES8311 microphone gain=24 dB (API result=%d); software gain=2x, soft limiter enabled",
          mic_gain_status);
 // Speaker enable GPIO resides on 0x43 PI4IOE expander, not ESP32 GPIO18.
 i2c_device_config_t exp={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x43,.scl_speed_hz=100000};
 if(i2c_master_bus_add_device(ext_bus,&exp,&amp)==ESP_OK){
  ext_write(0x07,0x00);ext_write(0x0d,0xff);ext_write(0x03,0x6f);set_amp(false);
 } else ESP_LOGW(TAG,"PI4IOE amp expander not detected (speaker may be silent)");
 ESP_LOGI(TAG,"ES8311 initialized: I2S GPIO BCLK8 WS6 DIN7 DOUT5, I2C SDA38 SCL39");
 return true;
}
bool bober_audio_suspend(void){
 // Called only after both I2S owners have acknowledged their safe points.
 bool ok=!amp||ext_write(0x05,0xfe)==ESP_OK;
 if(codec_dev&&esp_codec_dev_close(codec_dev)!=ESP_CODEC_DEV_OK)ok=false;
 // The codec data interface may already have disabled the channels.
 esp_err_t r=rx?i2s_channel_disable(rx):ESP_OK;
 esp_err_t t=tx?i2s_channel_disable(tx):ESP_OK;
 return ok&&(r==ESP_OK||r==ESP_ERR_INVALID_STATE)&&(t==ESP_OK||t==ESP_ERR_INVALID_STATE);
}
static inline int16_t clamp(int32_t x){return x>32767?32767:(x<-32768?-32768:x);}
// Input is ES8311 mono in LEFT I2S slot (observed: L non-zero, R always zero).
// Convert 32-bit slot to PCM16, high-pass to remove DC, apply 2x gain and a
// soft limiter. Gate silence to zero samples; preserve the continuous 16 kHz
// uplink required by ElevenLabs server VAD (never pause the PCM clock).
static inline int16_t limit_pcm(int32_t v){
 // Smooth knee above +/-24000; saturates at 32767 with no hard flat-top.
 const int32_t knee=24000;
 if(v>knee){v=knee+(v-knee)*8767/(8767+(v-knee));}
 else if(v< -knee){int32_t a=-v-knee;v=-(knee+a*8767/(8767+a));}
 return clamp(v);
}
void bober_mic_task(void *arg){
 (void)arg;
 static int32_t frames[320*2];
 static int16_t pcm[320];
 uint64_t sum_l=0,sum_r=0;uint32_t peak_l=0,peak_r=0,frames_seen=0;
 uint32_t listening_frames=0,muted_frames=0,sent_chunks=0;
 int64_t stats_at=bober_now_ms();
 // Noise-floor estimator (in raw ES8311 PCM16 sample units).
 uint32_t floor_q8=12u<<8; // 8 fractional bits; avoid learning first loud syllable as noise
 int gate_hold=0;     // number of 20ms frames to keep gate open after speech
 int prev_phase=-1;
 bool wake_active=false,wake_checked=false;
 uint32_t wake_samples=0,wake_process_ms=0,wake_peak_l=0,wake_peak_r=0;
 float wake_peak_score=0,wake_raw_peak=0;
 int64_t wake_stats_at=bober_now_ms();
 while(1){
  bober_power_checkpoint(BOBER_POWER_MIC);
  size_t got=0;
  if(i2s_channel_read(rx,frames,sizeof(frames),&got,pdMS_TO_TICKS(200))!=ESP_OK||got<8)continue;
  bool listening=g_session_enabled&&g_ws_ready&&g_phase==BOBER_LISTENING;
  if(!listening)g_mic_level=0;
  size_t n=got/(sizeof(int32_t)*2);if(n>320)n=320;
  bool waiting=!g_session_enabled&&g_phase==BOBER_WAKE_WAIT&&!bober_power_requested();
  if(waiting){
   if(!wake_active){
    wake_active=lumi_wake_init();
    if(!wake_active){g_phase=BOBER_OFF;ESP_LOGE(TAG,"Wake detector unavailable; conversation remains off");continue;}
    if(!wake_checked){lumi_wake_self_test();wake_checked=true;}
    ESP_LOGI(TAG,"WAKE_WAIT: say Hej Lumi or Luuu-miiii; microphone stays local");
   }
   // Raw PCM16 before the conversation noise gate; feed every sample.
   for(size_t i=0;i<n;i++)pcm[i]=(int16_t)(frames[2*i]>>16);
   for(size_t i=0;i<n;i++){
    int32_t left=pcm[i],right=frames[2*i+1]>>16;
    uint32_t al=left<0?-left:left,ar=right<0?-right:right;
    if(al>wake_peak_l)wake_peak_l=al;
    if(ar>wake_peak_r)wake_peak_r=ar;
   }
   bool capturing=lumi_capture_feed(pcm,n);
   int64_t wake_begin=bober_now_ms();
   bool detected=lumi_wake_process(pcm,n);
   bool shaken=atomic_exchange(&shake_wake_requested,false);
   wake_process_ms+=(uint32_t)(bober_now_ms()-wake_begin);wake_samples+=(uint32_t)n;
   float wake_raw=lumi_wake_peak_score();if(wake_raw>wake_raw_peak)wake_raw_peak=wake_raw;
   float wake_score=lumi_wake_score();if(wake_score>wake_peak_score)wake_peak_score=wake_score;
   if(bober_now_ms()-wake_stats_at>=2000){
    ESP_LOGI(TAG,"WAKE_STATS samples=%lu process_ms=%lu peakL=%lu peakR=%lu score=%.3f max=%.3f raw=%.3f rms=%.1f ratio=%.4f speech=%d hej=%.3f lumiii=%.3f",
     (unsigned long)wake_samples,(unsigned long)wake_process_ms,(unsigned long)wake_peak_l,(unsigned long)wake_peak_r,
     (double)wake_score,(double)wake_peak_score,(double)wake_raw_peak,(double)lumi_wake_signal_rms(),(double)lumi_wake_signal_ratio(),lumi_wake_signal_ready(),(double)lumi_wake_score_for(1),(double)lumi_wake_score_for(2));
    wake_samples=wake_process_ms=wake_peak_l=wake_peak_r=0;wake_peak_score=wake_raw_peak=0;wake_stats_at=bober_now_ms();
   }
   if(!lumi_wake_healthy()){
    lumi_wake_deinit();wake_active=false;g_phase=BOBER_OFF;
    ESP_LOGE(TAG,"Wake inference failed; conversation remains off");
   }else if((detected||shaken)&&!capturing&&!bober_power_requested()){
    if(shaken)ESP_LOGI(TAG,"WAKE_DETECTED: strong shake");
    else ESP_LOGI(TAG,"WAKE_DETECTED: %s score=%.3f",lumi_wake_match()==2?"Luuu-miiii":"Hej Lumi",(double)lumi_wake_score());
    // Release inference buffers before TLS and TTS start allocating memory.
    lumi_wake_deinit();wake_active=false;
    if(g_uplink)xStreamBufferReset(g_uplink);
    g_generation++;g_ws_reconnect=false;g_phase=BOBER_CONNECTING;
    g_session_enabled=true;
   }
   continue;
  }
  atomic_store(&shake_wake_requested,false);
  if(wake_active){lumi_wake_deinit();wake_active=false;}

  uint64_t eng_l=0,eng_r=0;
  uint32_t max_l=0,max_r=0;
  for(size_t i=0;i<n;i++){
   int32_t sl=frames[2*i]>>16,sr=frames[2*i+1]>>16;
   uint32_t al=(uint32_t)(sl<0?-sl:sl),ar=(uint32_t)(sr<0?-sr:sr);
   eng_l+=al;eng_r+=ar;
   if(al>max_l)max_l=al;
   if(ar>max_r)max_r=ar;
  }
  if(listening){
   sum_l+=eng_l;sum_r+=eng_r;frames_seen+=(uint32_t)n;listening_frames+=(uint32_t)n;
   if(max_l>peak_l)peak_l=max_l;
   if(max_r>peak_r)peak_r=max_r;
  }
  if(g_phase!=prev_phase){
   // Never preserve a gate from a previous utterance or a speaker transition.
   gate_hold=0;dc_prev_x=0;dc_prev_y=0;prev_phase=g_phase;
  }
  int64_t now=bober_now_ms();
  if(listening){
   uint32_t level=(uint32_t)(eng_l/(n?n:1));
   // Track slow noise drift ONLY when the mic appears quiet.
   uint32_t noise=floor_q8>>8;
   if(level<noise*2+20){
    floor_q8=(floor_q8*63+(level<<8))/64;
    noise=floor_q8>>8;
   }
   // Threshold scales with measured background noise. At 24 dB mic gain,
   // quiet speech typically exceeds the background by a large factor.
   uint32_t threshold=noise*3+24;
   if(threshold<48)threshold=48;
   if(level>threshold){gate_hold=18;} // 360ms hangover for word endings
   else if(gate_hold)gate_hold--;
   bool reacting=lumi_reaction_busy();
   if(reacting){gate_hold=0;dc_prev_x=dc_prev_y=0;}
   bool pass=gate_hold>0 && !reacting;
   uint32_t motion_level=pass?level/8:0;
   if(motion_level>255)motion_level=255;
   g_mic_level=(uint8_t)((g_mic_level*2u+motion_level)/3u);
   if(!pass)muted_frames+=(uint32_t)n;
   for(size_t i=0;i<n;i++){
    int32_t x=frames[2*i]>>16;
    // 70 Hz high-pass; update continuously so the next open frame has no DC pop.
    int32_t y=x-dc_prev_x+(dc_prev_y*31900)/32768;
    dc_prev_x=x;dc_prev_y=y;
    pcm[i]=pass?limit_pcm(y*2):0;
   }
   if(xStreamBufferSend(g_uplink,pcm,n*2,0)!=n*2){
    ESP_LOGW(TAG,"microphone uplink FIFO overloaded; reconnect requested");g_ws_reconnect=true;
   }else sent_chunks++;
  }
  if(now-stats_at>=3000){
   ESP_LOGI(TAG,"MIC L avg=%lu peak=%lu | R avg=%lu peak=%lu | fixed=L gain=24dB +2x | noise=%lu threshold=%lu gated=%lu/%lu samples | chunks=%lu phase=%d",
    (unsigned long)(frames_seen?sum_l/frames_seen:0),(unsigned long)peak_l,
    (unsigned long)(frames_seen?sum_r/frames_seen:0),(unsigned long)peak_r,
    (unsigned long)(floor_q8>>8),
    (unsigned long)((floor_q8>>8)*3+24),
    (unsigned long)muted_frames,(unsigned long)listening_frames,
    (unsigned long)sent_chunks,(int)g_phase);
   stats_at=now;sum_l=sum_r=0;peak_l=peak_r=frames_seen=listening_frames=muted_frames=sent_chunks=0;
  }
 }
}
void bober_speaker_task(void *arg)
{
 (void)arg;
 static int16_t mono[320];
 static int32_t out[320*2];
 bool amp_on=false;
 while(1){
  if(bober_power_requested()){
   set_amp(false);lumi_reaction_read((uint8_t*)mono,sizeof(mono),false,(uint32_t)bober_now_ms());
   bober_power_checkpoint(BOBER_POWER_SPEAKER);
  }
  uint32_t generation=g_generation;
  bool speaking=g_ws_ready&&g_session_enabled&&g_phase==BOBER_SPEAKING;
  size_t n=lumi_reaction_read((uint8_t*)mono,sizeof(mono),g_session_enabled,(uint32_t)bober_now_ms());
  bool reacting=n>0||lumi_reaction_busy();
  if(!reacting)n=speaking?bober_pcm_pop((uint8_t*)mono,sizeof(mono)):0;
  speaking=speaking||reacting;
  if(n)g_last_play_ms=bober_now_ms();
  if(n<sizeof(mono))memset((uint8_t*)mono+n,0,sizeof(mono)-n);
  if(generation!=g_generation||!g_session_enabled||bober_power_requested()){
   memset(mono,0,sizeof(mono));speaking=false;
  }
  if(speaking!=amp_on){set_amp(speaking);amp_on=speaking;}
  uint32_t level=0;
  for(int i=0;i<320;i++){
   int32_t sample=mono[i];level+=(uint32_t)(sample<0?-sample:sample);
   out[2*i]=sample*65536;out[2*i+1]=sample*65536;
  }
  level=level/(320u*64u);if(level>255)level=255;
  g_speaker_level=speaking?(uint8_t)((g_speaker_level+level)/2):0;
  size_t written=0;
  i2s_channel_write(tx,out,sizeof(out),&written,pdMS_TO_TICKS(70));
  if(written<sizeof(out))vTaskDelay(pdMS_TO_TICKS(1));
 }
}
