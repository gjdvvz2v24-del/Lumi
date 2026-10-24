#include "orientation.h"
#include "animation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const int16_t poses[4][3]={
    {-16384,0,0},{0,16384,0},{16384,0,0},{0,-16384,0}
};
static void settle(bober_orientation_t *s,unsigned pose,uint32_t start)
{
    for (uint32_t dt=0;dt<=900;dt+=60)
        bober_orientation_update(s,poses[pose][0],poses[pose][1],0,start+dt);
    assert(s->rotation==pose);
}
int main(void)
{
    assert(bober_orientation_scale(4096)==16384);
    assert(bober_orientation_scale(32767)==32767);
    assert(bober_orientation_scale(-32768)==-32768);
    /* Every transition including half turns, boot held sideways and wake. */
    for (unsigned from=0;from<4;from++) for (unsigned to=0;to<4;to++) {
        bober_orientation_t s={0}; settle(&s,from,0); settle(&s,to,1000);
        bober_orientation_pause(&s); assert(s.rotation==to);
        settle(&s,from,3000);
    }
    bober_orientation_t s={0};settle(&s,1,0);
    /* Face up/down, a diagonal, free fall, shock: retain last rotation. */
    const int16_t ignored[][3]={{0,0,16384},{0,0,-16384},
                               {11585,11585,0},{0,0,0},{32767,32767,32767}};
    for (unsigned i=0;i<sizeof(ignored)/sizeof(ignored[0]);i++) {
        bober_orientation_pause(&s);
        for (unsigned t=0;t<2000;t+=60)
            assert(bober_orientation_update(&s,ignored[i][0],ignored[i][1],ignored[i][2],t)==1);
    }
    /* Brief 180-degree excursion cannot rotate the screen. */
    memset(&s,0,sizeof(s));
    for (unsigned t=0;t<=180;t+=60) assert(bober_orientation_update(&s,16384,0,0,t)==0);
    settle(&s,0,240);
    /* Long gaps must not count as a stable orientation interval. */
    bober_orientation_pause(&s);
    assert(bober_orientation_update(&s,0,16384,0,0)==0);
    assert(bober_orientation_update(&s,0,16384,0,1000)==0);
    assert(bober_orientation_update(&s,0,16384,0,1060)==0);
    settle(&s,1,1120);
    /* Timer wrap. */
    memset(&s,0,sizeof(s));settle(&s,3,UINT32_MAX-200);

    /* Build an independent expected image by FORWARD mapping source pixels.
       Production code uses inverse lookup into four-row DMA tiles. */
    static uint8_t frame[32768],expected[32768],tile[1024];
    for (unsigned y=0;y<128;y++) for (unsigned x=0;x<128;x++) {
        frame[2*(y*128+x)]=(uint8_t)x;
        frame[2*(y*128+x)+1]=(uint8_t)y;
    }
    for(unsigned r=0;r<4;r++) for(int dx=-4;dx<=4;dx++) for(int dy=-4;dy<=4;dy++) {
        for(int y=0;y<128;y++) for(int x=0;x<128;x++) {
            int sx=x-dx,sy=y-dy;
            if(sx<0) sx=0;
            if(sx>127) sx=127;
            if(sy<0) sy=0;
            if(sy>127) sy=127;
            int ox=x,oy=y;
            for(unsigned turn=0;turn<r;turn++) { int oldx=ox; ox=127-oy; oy=oldx; }
            expected[2*(oy*128+ox)]=(uint8_t)sx;
            expected[2*(oy*128+ox)+1]=(uint8_t)sy;
        }
        for(unsigned row=0;row<128;row+=4) {
            assert(bober_picture_rows_rotated(frame,tile,sizeof(tile),row,4,dx,dy,r));
            assert(!memcmp(tile,expected+row*256,sizeof(tile)));
        }
    }
    assert(!bober_picture_rows_rotated(frame,tile,1023,0,4,0,0,1));
    assert(!bober_picture_rows_rotated(frame,tile,1024,127,4,0,0,1));
    assert(!bober_picture_rows_rotated(frame,tile,1024,0,4,0,0,4));
    assert(!bober_picture_rows_rotated(frame,tile,1024,0,4,5,0,1));
    puts("PASS: all orientations/transitions; flat/shock/jitter/gap/wake/clock wrap; 324 full RGB565 transforms");
}
