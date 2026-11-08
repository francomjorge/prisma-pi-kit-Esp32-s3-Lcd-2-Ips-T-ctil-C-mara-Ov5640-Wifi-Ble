#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* CST816D 240x320; pure-state classifier, host-tested.
 * Invalid or interrupted I2C reads never become "finger released".
 * 0=none, 1=tap, 2=swipe left, 3=swipe right.
 */
enum {
    TOUCHZ_WIDTH=240, TOUCHZ_HEIGHT=320,
    TOUCHZ_SWIPE_PX=29,
    TOUCHZ_TAP_SLOP_PX=13,
    TOUCHZ_CONTACT_MIN_MS=65,
    TOUCHZ_TAP_MAX_MS=650,
    TOUCHZ_SWIPE_MAX_MS=1100,
    TOUCHZ_ACTION_GUARD_MS=260,
    TOUCHZ_STALE_MS=350
};
typedef struct {
    bool pressed;
    bool consumed;
    bool pending;
    uint8_t down_samples;
    uint8_t up_samples;
    uint16_t x0,y0,x,y;
    uint16_t last_x,last_y;
    uint16_t action_x,action_y;
    uint64_t first_down_ms;
    uint64_t last_sample_ms;
    uint64_t last_action_ms;
    uint64_t last_release_ms;
    bool emitted;
} touchz_t;

static inline void touchz_init(touchz_t *s) { *s=(touchz_t){0}; }

static inline int touchz_release(touchz_t *s, uint64_t now)
{
    int event=0;
    const uint64_t duration=now-s->first_down_ms;
    const int dx=(int)s->x-(int)s->x0;
    const int dy=(int)s->y-(int)s->y0;
    const bool unlocked=(!s->emitted || now-s->last_action_ms >= TOUCHZ_ACTION_GUARD_MS);
    if(s->pressed && !s->consumed && unlocked && s->down_samples>=2) {
        if(duration>=TOUCHZ_CONTACT_MIN_MS && duration<=TOUCHZ_SWIPE_MAX_MS
           && abs(dx)>=TOUCHZ_SWIPE_PX && abs(dx)>=2*abs(dy))
            event=dx<0?2:3;
        else if(duration>=TOUCHZ_CONTACT_MIN_MS && duration<=TOUCHZ_TAP_MAX_MS
           && abs(dx)<=TOUCHZ_TAP_SLOP_PX && abs(dy)<=TOUCHZ_TAP_SLOP_PX
           && s->down_samples>=3)
            event=1;
    }
    if(event){
        s->action_x=(event==1?s->x0:s->x);
        s->action_y=(event==1?s->y0:s->y);
        s->last_action_ms=now;
        s->emitted=true;
    }
    s->pressed=false;s->pending=false;s->consumed=false;
    s->down_samples=0;s->up_samples=0;s->last_release_ms=now;
    return event;
}

static inline int touchz_feed(touchz_t *s, bool valid, bool finger,
                              uint16_t x, uint16_t y, uint64_t now)
{
    if(!valid) {
        /* A bus error is NOT a release. Drop stalled contacts without
         * producing a tap or swipe from stale coordinates. */
        if(s->pressed && now-s->last_sample_ms>TOUCHZ_STALE_MS){
            s->pressed=false;s->pending=false;s->consumed=false;
            s->down_samples=0;s->up_samples=0;s->last_release_ms=now;
        }
        return 0;
    }
    if(finger && (x>=TOUCHZ_WIDTH || y>=TOUCHZ_HEIGHT)) return 0;
    s->last_sample_ms=now;
    if(finger){
        s->up_samples=0;
        if(!s->pressed){
            /* First poll alone cannot create a tap. */
            s->pressed=true;s->pending=true;s->consumed=false;
            s->first_down_ms=now;
            s->x0=s->x=x;s->y0=s->y=y;
            s->down_samples=1;
            return 0;
        }
        if(s->down_samples<255)s->down_samples++;
        s->last_x=s->x;s->last_y=s->y;
        s->x=x;s->y=y;
        if(s->down_samples>=2)s->pending=false;
        const int dx=(int)x-(int)s->x0,dy=(int)y-(int)s->y0;
        if(!s->consumed && now-s->first_down_ms<=TOUCHZ_SWIPE_MAX_MS
           && now-s->first_down_ms>=TOUCHZ_CONTACT_MIN_MS
           && abs(dx)>=TOUCHZ_SWIPE_PX && abs(dx)>=2*abs(dy)
           && (!s->emitted || now-s->last_action_ms>=TOUCHZ_ACTION_GUARD_MS)){
            s->consumed=true; s->action_x=x;s->action_y=y;
            s->last_action_ms=now;s->emitted=true;
            return dx<0?2:3;
        }
        return 0;
    }
    if(!s->pressed)return 0;
    if(++s->up_samples<2)return 0; /* Debounce release, not just the action. */
    return touchz_release(s,now);
}
