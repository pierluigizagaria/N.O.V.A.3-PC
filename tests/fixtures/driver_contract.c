#include "driver_contract.h"
static float clamp(float x,float lo,float hi) { return x<lo ? lo : x>hi ? hi : x; }
static float absolute(float x) { return x<0 ? -x : x; }
void driver_camera_body(struct DriverContract *s,unsigned dt) {
    float x=s->raw_x*s->scale_x, y=s->raw_y*s->scale_y;
    ++s->calls;
    if(s->accelerated) x*=clamp((absolute(s->raw_x)+1)*s->gain,1,10);
    /* The native consumer caches both input values before the one-shot clear. */
    if(s->one_shot) s->raw_x=s->raw_y=0;
    s->yaw-=x;
    y*=((s->shooter ? s->shooter_distance : s->driver_distance)>=0 ? 1 : -1);
    y*=s->invert_native ? 1 : -1;
    s->pitch=clamp(s->pitch+y,s->pitch_minimum,s->pitch_maximum);
    if(!x && !y) {
        /* A native no-motion recentering witness; dt belongs to this path. */
        float step=(float)dt*.001f;
        if(s->yaw>step) s->yaw-=step; else if(s->yaw< -step) s->yaw+=step;
    }
    if(s->mode==0) s->steering=clamp(s->yaw*.05f,-1,1);
    if(s->mutate) { s->raw_x=99; s->raw_y=-27; }
}
void driver_pedals_body(struct DriverContract *s) {
    ++s->pedal_calls;
    if(!s->ingame || !s->enabled || s->mode!=1) return;
    {
        unsigned accelerator=s->accelerator,brake=s->brake_pedal;
        unsigned (*query)(struct DriverContract *,unsigned)=(void *)s->query;
        if(query) accelerator=query(s,0);
        s->accelerator_timer=accelerator ? s->accelerator_timer+(int)s->dt : 0;
        if(query) brake=query(s,1);
        s->brake_timer=brake ? s->brake_timer+(int)s->dt : 0;
    }
}
void driver_event_body(struct DriverContract *s,unsigned type,float x,float y) {
    ++s->events;
    if(type==14) { s->native_x=s->native_y=0; ++s->releases; }
    else if(type==13) { s->native_x=x; s->native_y=-y; }
    else __builtin_trap();
}
void driver_draw_body(struct DriverDrawContract *s,unsigned alpha) { ++s->draws; s->alpha=alpha; }
void driver_fullscreen_body(unsigned *calls) { ++*calls; }
