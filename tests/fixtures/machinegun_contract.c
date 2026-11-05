#include "machinegun_contract.h"
extern void machinegun_primary_stop(struct MachineGunWeapon *,unsigned,unsigned);
extern void machinegun_primary_release(struct MachineGunManager *,unsigned);
static float wrap(float a) { while(a>180) a-=360; while(a< -180) a+=360; return a; }
static float clamp(float a,float low,float high) { return a<low ? low : a>high ? high : a; }
static void yaw(struct MachineGunContract *s,float delta) {
    s->yaw=clamp(wrap(s->yaw+delta),-s->yaw_limit,s->yaw_limit);
}
static void pitch(struct MachineGunContract *s,float delta) {
    s->pitch=clamp(wrap(s->pitch+delta),s->pitch_minimum,s->pitch_maximum);
}
void machinegun_consume_body(struct MachineGunContract *s,unsigned dt) {
    ++s->consumers;
    if(s->look.yaw_pending) { yaw(s,s->look.yaw_target-s->yaw); s->look.yaw_pending=0; }
    if(s->look.yaw_rate!=s->look.yaw_rate_target) s->look.yaw_rate+=s->look.yaw_rate_step;
    if(s->look.pitch_override) pitch(s,s->look.pitch_override);
    else if((s->look.pitch_delta>0 && s->pitch<s->look.pitch_target) ||
            (s->look.pitch_delta<0 && s->pitch>s->look.pitch_target)) {
        float delta=s->look.pitch_delta;
        pitch(s,delta);
        if((delta>0 && s->pitch>=s->look.pitch_target) || (delta<0 && s->pitch<=s->look.pitch_target)) {
            pitch(s,s->look.pitch_target-s->pitch); s->look.pitch_delta=0;
        }
    }
    yaw(s,-s->look.yaw_rate*(float)dt*.02f);
}
void machinegun_update_body(struct MachineGunContract *s,unsigned dt) {
    (void)dt;
    if(s->exiting) return;
    if(s->released[0]) {
        machinegun_primary_stop(&s->weapon,0,1);
        machinegun_primary_release(&s->manager,0);
        s->released[0]=0;
    }
    if((s->pressed[0] || s->held[0]) && s->weapon.allowed[0]) {
        ++s->weapon.shots[0]; s->weapon.firing=1;
        ++s->manager.presses; s->manager.firing=1;
    }
    if(s->pressed[1] && s->weapon.allowed[1]) ++s->weapon.shots[1];
    s->pressed[0]=s->pressed[1]=0;
}
void machinegun_event_body(struct MachineGunContract *s,unsigned type) {
    unsigned channel,down;
    if(type>32) __builtin_trap();
    ++s->events[type];
    if(type<29) return;
    channel=(type-29)/2; down=type&1;
    s->held[channel]=s->pressed[channel]=down; s->released[channel]=!down;
}
void machinegun_reset_body(struct MachineGunContract *s) {
    unsigned i; ++s->resets;
    for(i=0;i<2;++i) s->held[i]=s->pressed[i]=s->released[i]=0;
    s->look.yaw_rate=s->look.yaw_rate_target=s->look.yaw_rate_step=0;
    s->look.yaw_target=s->yaw; s->look.yaw_pending=0;
    s->look.pitch_target=s->pitch; s->look.pitch_delta=s->look.pitch_override=0;
}
void machinegun_stop_body(struct MachineGunWeapon *s,unsigned channel,unsigned messages) {
    if(channel) return;
    ++s->stops; s->messages+=messages!=0; s->firing=0;
}
void machinegun_release_body(struct MachineGunManager *s,unsigned channel) {
    if(channel) return;
    ++s->releases; s->firing=0;
}
