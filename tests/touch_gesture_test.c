#include <assert.h>
#include <stdio.h>
#include "touch_gesture.h"

static void down(touchz_t*s,unsigned x,unsigned y,uint64_t at){
 assert(touchz_feed(s,true,true,x,y,at)==0);
}
static void up(touchz_t*s,uint64_t at){
 assert(touchz_feed(s,true,false,0,0,at)==0);
}

static void tap_and_phantom(void){
 touchz_t s;touchz_init(&s);
 down(&s,130,160,100);up(&s,120);up(&s,140);
 assert(!s.emitted); /* One transient down sample is not a tap. */
 down(&s,130,170,500);down(&s,131,170,520);down(&s,130,171,550);
 up(&s,580);
 assert(touchz_feed(&s,true,false,0,0,605)==1);
 assert(s.action_x==130&&s.action_y==170);
 assert(touchz_feed(&s,true,false,0,0,630)==0); /* No second click */
 down(&s,120,120,700);down(&s,121,120,725);down(&s,122,121,750);
 up(&s,760);assert(touchz_feed(&s,true,false,0,0,780)==0);
 /* Press too short, regardless of repeated polls. */
 down(&s,125,150,1500);down(&s,125,150,1510);down(&s,125,150,1515);
 up(&s,1520);assert(touchz_feed(&s,true,false,0,0,1540)==0);
}
static void short_swipe(void){
 touchz_t s;touchz_init(&s);
 down(&s,145,160,100);down(&s,137,160,120);
 assert(touchz_feed(&s,true,true,114,162,178)==2);
 assert(s.action_x==114); /* 31 px horizontal */
 assert(touchz_feed(&s,true,true,85,162,205)==0);
 up(&s,240);assert(touchz_feed(&s,true,false,0,0,260)==0);
 down(&s,55,130,800);down(&s,65,130,830);
 assert(touchz_feed(&s,true,true,88,133,900)==3);
 up(&s,925);assert(touchz_feed(&s,true,false,0,0,960)==0);
}
static void release_swipe_and_noise(void){
 touchz_t s;touchz_init(&s);
 down(&s,140,180,100);down(&s,130,180,120);
 /* Only last sample crosses 29px; release still detects a swipe. */
 assert(touchz_feed(&s,true,true,108,180,145)==0);
 up(&s,160);assert(touchz_feed(&s,true,false,0,0,180)==2);
 touchz_init(&s);
 down(&s,110,170,100);down(&s,109,168,120);
 assert(touchz_feed(&s,false,false,0,0,600)==0);
 assert(!s.pressed); /* I2C outage is not a release/tap. */
 assert(touchz_feed(&s,true,false,0,0,625)==0);
 /* Strong vertical movement cannot navigate. */
 down(&s,140,90,1000);down(&s,139,117,1030);
 assert(touchz_feed(&s,true,true,110,178,1120)==0);
 up(&s,1150);assert(touchz_feed(&s,true,false,0,0,1170)==0);
 touchz_init(&s);
 assert(touchz_feed(&s,true,true,800,400,50)==0); /* invalid coords */
 assert(!s.pressed);
}
int main(void){
 tap_and_phantom();short_swipe();release_swipe_and_noise();
 puts("CS010Z TOUCH PASS: release-only taps, 29px short swipes, debounced contact, no phantom double clicks");
}
