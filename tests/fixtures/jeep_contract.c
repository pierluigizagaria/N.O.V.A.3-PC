#include "jeep_contract.h"
#include "driver_contract.h"
extern void jeep_primary_stop(struct JeepWeapon *,unsigned,unsigned);
extern void jeep_secondary_stop(struct JeepWeapon *,unsigned,unsigned);
extern void jeep_primary_animation(struct JeepAnimation *);
extern void jeep_secondary_animation(struct JeepAnimation *);
static void fire(struct JeepWeapon *w,unsigned channel) {
    w->last_channel=channel;
    if(w->allowed[channel] && w->ammo[channel] && !w->cooldown[channel]) {
        --w->ammo[channel]; ++w->shots[channel]; w->firing=1;
    }
}
void jeep_event_body(struct JeepContract *s,unsigned type) {
    unsigned channel,down;
    if(type>32) __builtin_trap();
    ++s->events[type];
    if(type<29) return;
    channel=(type-29)/2; down=type&1;
    s->held[channel]=s->pressed[channel]=down; s->released[channel]=!down;
}
void jeep_reset_body(struct JeepContract *s) {
    unsigned i; ++s->resets;
    for(i=0;i<2;++i) s->held[i]=s->pressed[i]=s->released[i]=0;
    if(s->driver) {
        struct DriverContract *d=(void *)s->driver;
        d->raw_x=d->raw_y=d->native_x=d->native_y=0;
    }
}
void jeep_stop_body(struct JeepWeapon *w,unsigned channel,unsigned messages) {
    w->last_channel=channel; w->last_messages=messages;
    if(channel>1) return;
    // A stop emits messages before the native audio-handle guard. Repetition
    // therefore matters even if the weapon is no longer firing.
    ++w->stops[channel]; w->messages[channel]+=messages!=0; w->firing=0;
    if(w->on_stop) ((void (*)(void))w->on_stop)();
}
void jeep_animation_body(struct JeepAnimation *a) {
    if(a->enabled && a->channel<2) { ++a->stops[a->channel]; a->state=0; }
}
void jeep_update_body(struct JeepContract *s,unsigned dt) {
    unsigned channel; (void)dt; ++s->updates;
    if(!s->enabled || !s->shooter) return;
    for(channel=0;channel<2;++channel) {
        if(channel && !s->secondary) continue;
        if(s->pressed[channel]) {
            s->pressed[channel]=0;
            if(!s->weapon.firing) fire(&s->weapon,channel);
            ++s->animation.starts[channel]; s->animation.state=2;
        } else if(s->held[channel] && !s->weapon.firing) fire(&s->weapon,channel);
        if(s->released[channel]) {
            s->released[channel]=0;
            if(channel==0) jeep_primary_stop(&s->weapon,0,1);
            else jeep_secondary_stop(&s->weapon,1,1);
            if(s->halt==channel+1) return;
            s->animation.channel=channel;
            if(channel==0) jeep_primary_animation(&s->animation);
            else jeep_secondary_animation(&s->animation);
        }
    }
}
