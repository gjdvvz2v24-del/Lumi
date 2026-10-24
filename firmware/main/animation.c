#include "animation.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

bool bober_animation_open(bober_animation_t *a, const uint8_t *p, size_t n) {
    if (!a) return false;
    memset(a, 0, sizeof(*a));
    if (!p || n < 18 || memcmp(p,"BANI",4) || u16(p+4)!=1 ||
        u16(p+6)!=128 || u16(p+8)!=128 || !u16(p+10) ||
        u16(p+10)>30 || u16(p+12)!=64) return false;
    uint32_t count=u32(p+14);
    if (!count || count>(n-18)/8) return false;
    size_t expected=18+(size_t)count*8;
    for (uint32_t i=0;i<count;i++) {
        size_t off=u32(p+18+(size_t)i*8), len=u32(p+22+(size_t)i*8);
        if (off!=expected || off>n || len<130 || len>n-off) return false;
        expected=off+len;
    }
    if (expected!=n) return false;
    *a=(bober_animation_t){.data=p,.size=n,.width=128,.height=128,.fps=u16(p+10),.count=count};
    return true;
}

bool bober_animation_decode(const bober_animation_t *a, uint32_t frame,
                            uint8_t *dst, size_t capacity) {
    if (!a || !a->data || frame>=a->count || !dst || capacity<32768) return false;
    const uint8_t *entry=a->data+18+(size_t)frame*8;
    size_t off=u32(entry), len=u32(entry+4);
    if (off>a->size || len<130 || len>a->size-off) return false;
    const uint8_t *palette=a->data+off, *p=palette+128, *end=palette+len;
    size_t pixel=0;
    while (p<end && pixel<16384) {
        uint8_t command=*p++;
        size_t count=(command&127)+1u;
        if (count>16384-pixel) return false;
        bool run=(command&128)!=0;
        if ((size_t)(end-p)<(run?1u:count)) return false;
        for (size_t i=0;i<count;i++) {
            uint8_t color=run?*p:p[i];
            if (color>=64) return false;
            dst[2*pixel]=palette[2*color+1];
            dst[2*pixel+1]=palette[2*color];
            ++pixel;
        }
        p+=run?1u:count;
    }
    return pixel==16384 && p==end;
}

bool bober_picture_rows(const uint8_t *frame, uint8_t *tile, size_t capacity,
                        unsigned first_row, unsigned rows, int dx, int dy)
{
    return bober_picture_rows_rotated(frame,tile,capacity,first_row,rows,dx,dy,0);
}

bool bober_picture_rows_rotated(const uint8_t *frame, uint8_t *tile, size_t capacity,
                                unsigned first_row, unsigned rows, int dx, int dy,
                                unsigned rotation)
{
    if (!frame||!tile||first_row>=128||rows>128-first_row||capacity<(size_t)rows*256||
        dx< -4||dx>4||dy< -4||dy>4||rotation>3) return false;
    if (!dx&&!dy&&!rotation) { memcpy(tile,frame+(size_t)first_row*256,(size_t)rows*256);return true; }
    for (unsigned y=0;y<rows;y++) {
        int screen_y=(int)(first_row+y);
        for (int x=0;x<128;x++) {
            int sx,sy;
            switch (rotation) {
                case 1: sx=screen_y; sy=127-x; break;
                case 2: sx=127-x; sy=127-screen_y; break;
                case 3: sx=127-screen_y; sy=x; break;
                default: sx=x; sy=screen_y; break;
            }
            sx-=dx; sy-=dy;
            if(sx<0)sx=0;else if(sx>127)sx=127;
            if(sy<0)sy=0;else if(sy>127)sy=127;
            size_t source=((size_t)sy*128+(size_t)sx)*2;
            size_t dest=((size_t)y*128+(size_t)x)*2;
            tile[dest]=frame[source];tile[dest+1]=frame[source+1];
        }
    }
    return true;
}
