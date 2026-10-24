#include "interaction.h"

bober_button_event_t bober_button_update(bober_button_state_t *s, bool pressed, uint32_t now)
{
    if (s->raw_pressed!=pressed) { s->raw_pressed=pressed; s->changed_at=now; }
    if (s->raw_pressed!=s->stable_pressed && (uint32_t)(now-s->changed_at)>=40) {
        s->stable_pressed=s->raw_pressed;
        if (s->stable_pressed) { s->pressed_at=now; s->hold_sent=false; }
        else if (!s->hold_sent) return BOBER_BUTTON_CLICK;
    }
    if (s->stable_pressed && !s->hold_sent && (uint32_t)(now-s->pressed_at)>=1200) {
        s->hold_sent=true;
        return BOBER_BUTTON_HOLD;
    }
    return BOBER_BUTTON_NONE;
}

void bober_picture_motion(uint32_t ms, bool speaking, bool listening, uint8_t level, int *dx, int *dy)
{
    *dx=0; *dy=0;
    if (speaking) {
        int amplitude=1+level/100;
        int wave=(int)((ms/80)%4);
        *dx=(wave==0?-1:wave==2?1:0)*amplitude;
        *dy=(int)((ms/180)%3)-1;
    } else if (listening && level>4) {
        *dy=(int)((ms/400)%4);
        *dy=(*dy==0?-1:*dy==2?1:0);
        if (level>4) *dx=((ms/140)%2)?1:-1;
    } else {
        *dy=((ms/600)%2)?1:0;
    }
}

uint32_t lumi_slideshow_update(lumi_slideshow_t *s,uint32_t now,bool voice,unsigned count)
{
    if(!s->initialized){s->initialized=true;s->quiet_since=now;}
    if(voice)s->quiet_since=now;
    else if(count && (uint32_t)(now-s->quiet_since)>=3000){
        s->frame=(s->frame+1)%count;s->quiet_since=now;
    }
    return s->frame;
}

/* Strong shake: three alternating acceleration peaks above 2g after removing
   gravity. A single knock, gentle movement or slow rotation cannot wake Lumi. */
lumi_reaction_t lumi_motion_update(lumi_motion_state_t *s,int16_t x,int16_t y,int16_t z,uint32_t now)
{
    int32_t raw[3]={x,y,z},dynamic[3];
    bool fresh=s->initialized && (uint32_t)(now-s->sample_at)<=100;
    s->sample_at=now;s->initialized=true;
    if(!fresh){
        for(unsigned i=0;i<3;i++)s->gravity_q8[i]=raw[i]*256;
        s->pulses=0;return LUMI_REACTION_NONE;
    }
    int64_t energy=0,dot=0;
    for(unsigned i=0;i<3;i++){
        s->gravity_q8[i]+=(raw[i]*256-s->gravity_q8[i])/16;
        dynamic[i]=raw[i]-s->gravity_q8[i]/256;
        energy+=(int64_t)dynamic[i]*dynamic[i];
        dot+=(int64_t)dynamic[i]*s->last_pulse[i];
    }
    if(s->cooling && (uint32_t)(now-s->cooldown_at)<4000)return LUMI_REACTION_NONE;
    s->cooling=false;
    if(s->pulses && ((uint32_t)(now-s->pulse_at)>350 || (uint32_t)(now-s->started_at)>900))s->pulses=0;
    if(energy<8192LL*8192)return LUMI_REACTION_NONE;
    if(s->pulses && ((uint32_t)(now-s->pulse_at)<60 || dot>=0))return LUMI_REACTION_NONE;
    if(!s->pulses)s->started_at=now;
    for(unsigned i=0;i<3;i++)s->last_pulse[i]=dynamic[i];
    s->pulse_at=now;
    if(++s->pulses<3)return LUMI_REACTION_NONE;
    s->pulses=0;s->cooling=true;s->cooldown_at=now;
    return LUMI_REACTION_SHAKE;
}
