#include "mech_contract.h"
extern void contract_consume_body(struct ContractInput *,struct ContractActor *,struct ContractObservation *);
extern void mech_primary_stop(struct MechContract *,unsigned,unsigned);
extern void mech_secondary_stop(struct MechContract *,unsigned,unsigned);
void mech_stop_body(struct MechContract *s,unsigned channel,unsigned messages) {
    s->last_channel=channel; s->last_message_argument=messages;
    if (channel > 1) return;
    s->firing[channel]=0;
    ++s->stops[channel];
    s->messages[channel]+=messages!=0;
}
void mech_event_body(struct MechContract *s,unsigned type,float x,float y) {
    if (type>64) __builtin_trap();
    ++s->events[type];
    if (type==13) { s->movement[0]=x; s->movement[1]=y; }
    if (type==14) s->movement[0]=s->movement[1]=0;
    if (type>=29 && type<=32) {
        unsigned channel=(type-29)/2, down=(type&1)!=0;
        s->held[channel]=s->pressed[channel]=down;
        s->released[channel]=!down;
    }
}
void mech_reset_body(struct MechContract *s) {
    unsigned i;
    ++s->resets;
    for(i=0;i<2;++i) s->held[i]=s->pressed[i]=s->released[i]=0;
    s->movement[0]=s->movement[1]=0;
    s->look.yaw_rate=s->look.yaw_rate_target=s->look.yaw_rate_step=0;
    s->look.yaw_target=s->actor.yaw; s->look.yaw_pending=0;
    s->look.pitch_target=s->actor.pitch;
    s->look.pitch_delta=s->look.pitch_override=0;
}
void mech_consume_body(struct MechContract *s,unsigned dt) {
    unsigned i;
    s->seen.milliseconds=dt; s->seen.limit=20;
    contract_consume_body(&s->look,&s->actor,&s->seen);
    if (!s->seen.locked) {
        if (s->actor.pitch>20) s->actor.pitch=20;
        if (s->actor.pitch< -20) s->actor.pitch=-20;
    }
    for(i=0;i<2;++i) {
        if(s->held[i] && s->allowed[i]) { s->firing[i]=1; ++s->shots[i]; }
        if(s->released[i]) {
            if(i==0) mech_primary_stop(s,i,1); else mech_secondary_stop(s,i,1);
            s->released[i]=0;
        }
        s->pressed[i]=0;
    }
}
void mech_set_draw_body(struct MechDrawControl *s,unsigned visible) {
    s->visible=visible; ++s->sets;
}
