#include "interaction.h"
#include "animation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bober_button_event_t click(bober_button_state_t *s,uint32_t t)
{
    assert(bober_button_update(s,true,t)==BOBER_BUTTON_NONE);
    assert(bober_button_update(s,true,t+40)==BOBER_BUTTON_NONE);
    assert(bober_button_update(s,false,t+140)==BOBER_BUTTON_NONE);
    return bober_button_update(s,false,t+180);
}
int main(void)
{
    bober_button_state_t s={0};
    assert(click(&s,100)==BOBER_BUTTON_CLICK);
    assert(click(&s,500)==BOBER_BUTTON_CLICK);
    assert(bober_button_update(&s,true,900)==BOBER_BUTTON_NONE);
    assert(bober_button_update(&s,true,940)==BOBER_BUTTON_NONE);
    assert(bober_button_update(&s,true,2139)==BOBER_BUTTON_NONE);
    assert(bober_button_update(&s,true,2140)==BOBER_BUTTON_HOLD);
    assert(bober_button_update(&s,true,5000)==BOBER_BUTTON_NONE);
    assert(bober_button_update(&s,false,5010)==BOBER_BUTTON_NONE);
    assert(bober_button_update(&s,false,5050)==BOBER_BUTTON_NONE); /* no click on hold release */
    assert(click(&s,5200)==BOBER_BUTTON_CLICK);
    /* Contact bounce: presses shorter than 40ms cannot switch modes. */
    memset(&s,0,sizeof(s));
    for(unsigned i=0;i<20;i++)assert(bober_button_update(&s,i%2,i*10)==BOBER_BUTTON_NONE);
    assert(bober_button_update(&s,false,200)==BOBER_BUTTON_NONE);
    assert(bober_button_update(&s,false,250)==BOBER_BUTTON_NONE);
    /* Millisecond counter rollover during a press. */
    memset(&s,0,sizeof(s));
    assert(click(&s,UINT32_MAX-100)==BOBER_BUTTON_CLICK);
    for(unsigned t=0;t<6000;t+=17){
        int x,y;bober_picture_motion(t,true,false,255,&x,&y);assert(x>=-3&&x<=3&&y>=-1&&y<=1);
        bober_picture_motion(t,false,true,255,&x,&y);assert(x>=-1&&x<=1&&y>=-1&&y<=1);
    }
    /* Screen shifting preserves 16-bit pixel bytes and extends edges. */
    uint8_t frame[32768],tile[1024];
    for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++){
        frame[(y*128+x)*2]=(uint8_t)x;frame[(y*128+x)*2+1]=(uint8_t)y;
    }
    for(int dx=-3;dx<=3;dx++)for(int dy=-1;dy<=1;dy++)for(unsigned row=0;row<128;row+=4){
        assert(bober_picture_rows(frame,tile,sizeof(tile),row,4,dx,dy));
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<128;x++){
            int sx=(int)x-dx,sy=(int)(row+y)-dy;
            if(sx<0)sx=0;
            if(sx>127)sx=127;
            if(sy<0)sy=0;
            if(sy>127)sy=127;
            assert(tile[(y*128+x)*2]==sx&&tile[(y*128+x)*2+1]==sy);
        }
    }
    assert(!bober_picture_rows(frame,tile,1023,0,4,0,0));
    puts("PASS: click/hold/wake/bounce/rollover; voice motion; shifted RGB565 tiles");
    return 0;
}
