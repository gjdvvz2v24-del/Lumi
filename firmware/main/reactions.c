#include "reactions.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

extern const uint8_t impact_start[] asm("_binary_lumi_impact_pcm_start");
extern const uint8_t impact_end[] asm("_binary_lumi_impact_pcm_end");
extern const uint8_t shake_start[] asm("_binary_lumi_shake_pcm_start");
extern const uint8_t shake_end[] asm("_binary_lumi_shake_pcm_end");
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static lumi_reaction_t pending;
static bool busy;
/* Only speaker task accesses cursor/tail. */
static const uint8_t *cursor,*end;
static bool tail;
static uint32_t tail_at;

void lumi_reaction_request(lumi_reaction_t event)
{
    if(event!=LUMI_REACTION_IMPACT)return;
    portENTER_CRITICAL(&lock);
    if(!busy){pending=event;busy=true;}
    portEXIT_CRITICAL(&lock);
}
bool lumi_reaction_busy(void)
{
    portENTER_CRITICAL(&lock);
    bool value=busy;
    portEXIT_CRITICAL(&lock);
    return value;
}
size_t lumi_reaction_read(uint8_t *out,size_t capacity,bool enabled,uint32_t now)
{
    portENTER_CRITICAL(&lock);
    lumi_reaction_t event=pending;pending=LUMI_REACTION_NONE;
    if(!enabled)busy=false;
    portEXIT_CRITICAL(&lock);
    if(!enabled){cursor=end=NULL;tail=false;return 0;}
    if(event){
        cursor=event==LUMI_REACTION_IMPACT?impact_start:shake_start;
        end=event==LUMI_REACTION_IMPACT?impact_end:shake_end;
        tail=false;
    }
    if(cursor && cursor<end){
        size_t n=(size_t)(end-cursor);if(n>capacity)n=capacity;n&=~(size_t)1;
        memcpy(out,cursor,n);cursor+=n;
        if(cursor==end){tail=true;tail_at=now;}
        return n;
    }
    /* Speaker DMA and room echo settle before microphone opens again. */
    if(tail && (uint32_t)(now-tail_at)>=180){
        tail=false;cursor=end=NULL;
        portENTER_CRITICAL(&lock);busy=false;portEXIT_CRITICAL(&lock);
    }
    return 0;
}
