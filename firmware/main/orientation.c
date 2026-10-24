#include "orientation.h"

void bober_orientation_pause(bober_orientation_t *s)
{
    if (s) { s->filtered=false; s->pending=false; }
}

unsigned bober_orientation_update(bober_orientation_t *s, int16_t x, int16_t y,
                                  int16_t z, uint32_t now)
{
    if (!s) return 0;
    /* Reject free fall/shaking before filtering (0.75..1.25 g). */
    int64_t norm=(int64_t)x*x+(int64_t)y*y+(int64_t)z*z;
    if (norm<151000000LL || norm>420000000LL) {
        bober_orientation_pause(s); return s->rotation;
    }
    if (!s->filtered || (uint32_t)(now-s->last_sample)>250) {
        s->x=x; s->y=y; s->z=z; s->filtered=true; s->pending=false;
    } else {
        s->x+=(x-s->x)/2; s->y+=(y-s->y)/2; s->z+=(z-s->z)/2;
    }
    s->last_sample=now;
    int32_t ax=s->x<0?-s->x:s->x, ay=s->y<0?-s->y:s->y;
    int32_t dominant=ax>ay?ax:ay, other=ax>ay?ay:ax;
    /* Hold near flat and around diagonal boundaries. Entry ~51 degrees. */
    if (dominant<7373 || dominant*100<other*125) {
        s->pending=false; return s->rotation;
    }
    /* M5Unified AtomS3R mapping: board X=raw Y, board Y=-raw X.
       Acceleration points UP at rest; native screen y points DOWN. */
    unsigned candidate=ax>=ay?(s->x<0?0u:2u):(s->y>0?1u:3u);
    if (candidate==s->rotation) { s->pending=false; return s->rotation; }
    if (!s->pending || candidate!=s->candidate) {
        s->candidate=candidate; s->candidate_since=now; s->pending=true;
    } else if ((uint32_t)(now-s->candidate_since)>=250) {
        s->rotation=candidate; s->pending=false;
    }
    return s->rotation;
}

int16_t bober_orientation_scale(int16_t sample)
{
    int32_t value=(int32_t)sample*4;
    return value>32767?32767:value< -32768?-32768:(int16_t)value;
}
