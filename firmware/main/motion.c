#include "bober.h"
#include "imu.h"
#include "orientation.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

static portMUX_TYPE sample_lock=portMUX_INITIALIZER_UNLOCKED;
static int16_t latest_x,latest_y,latest_z;
static uint32_t latest_ms;
static bool valid;

bool bober_motion_latest(int16_t *x,int16_t *y,int16_t *z,uint32_t now)
{
    portENTER_CRITICAL(&sample_lock);
    bool fresh=valid&&(uint32_t)(now-latest_ms)<=100;
    if(fresh){*x=latest_x;*y=latest_y;*z=latest_z;}
    portEXIT_CRITICAL(&sample_lock);
    return fresh;
}
void bober_motion_task(void *arg)
{
    (void)arg;
    TickType_t tick=xTaskGetTickCount();
    lumi_motion_state_t reaction={0};
    ESP_LOGI("motion","Strong shake wake: 3 opposite peaks above 2g; no motion sounds");
    while(true){
        bober_power_checkpoint(BOBER_POWER_MOTION);
        int16_t x,y,z;
        uint32_t now=(uint32_t)bober_now_ms();
        if(bober_imu_read(&x,&y,&z)){
            portENTER_CRITICAL(&sample_lock);
            latest_x=bober_orientation_scale(x);latest_y=bober_orientation_scale(y);
            latest_z=bober_orientation_scale(z);latest_ms=now;valid=true;
            portEXIT_CRITICAL(&sample_lock);
            if(!g_session_enabled && g_phase==BOBER_WAKE_WAIT && !bober_power_requested()){
                if(lumi_motion_update(&reaction,x,y,z,now)==LUMI_REACTION_SHAKE)
                    bober_request_shake_wake();
            }else memset(&reaction,0,sizeof(reaction));
        }
        vTaskDelayUntil(&tick,pdMS_TO_TICKS(10));
    }
}
