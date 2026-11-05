#include "bike_contract.h"
void bike_base_body(struct BikeContract *s,unsigned dt) {
    ++s->base_calls; s->native_args[0]=dt;
    if(s->after_base) s->after_base(s);
}
void bike_move_body(struct BikeContract *s,unsigned dt) {
    ++s->updates; ++s->cursor;
    if(s->state==0) {
        float target=s->move_x*.01f,step=(float)dt*.001f;
        if(target>.6f) target=.6f; if(target< -.6f) target= -.6f;
        if(s->lean<target) { s->lean+=step; if(s->lean>target) s->lean=target; }
        if(s->lean>target) { s->lean-=step; if(s->lean<target) s->lean=target; }
        s->forward+=(float)dt*2.f; s->camera=s->forward*.1f+s->lean;
    } else if(s->state==1) { s->forward=s->script_target; s->camera=s->script_target*.2f; }
}
void bike_visual_body(struct BikeContract *s,unsigned quaternion) {
    ++s->visual_calls; s->native_args[1]=quaternion;
    if(s->after_visual) s->after_visual(s);
}
void bike_primary_body(struct BikeContract *s) {
    if(s->state==0 && s->pressed) { s->pressed=0; ++s->shots; }
}
void bike_event_body(struct BikeContract *s,unsigned type,float x) {
    if(type==13) { s->move_x=x*s->template_scale; ++s->events[0]; }
    if(type==14) { s->move_x=0; ++s->events[1]; }
    if(type==29) { s->held=s->pressed=1; s->released=0; ++s->events[2]; }
    if(type==30) { s->held=s->pressed=0; s->released=1; ++s->events[3]; }
}
void bike_state_body(struct BikeContract *s,unsigned state) { s->state=state; ++s->transitions; }
void bike_enable_body(struct BikeContract *s,unsigned value) { s->enabled=value; ++s->transitions; }
void bike_road_body(struct BikeContract *s,unsigned road) { s->cursor=road; ++s->transitions; }
void bike_configure_body(struct BikeContract *s,unsigned config) { s->template_scale=(float)config; ++s->transitions; }
void bike_move_to_body(struct BikeContract *s,unsigned position,unsigned direction,unsigned animation) {
    s->state=1; s->script_target=(float)position; s->native_args[1]=direction; s->native_args[2]=animation; ++s->transitions;
}
void bike_reset_body(struct BikeContract *s) { s->state=0; ++s->transitions; }
void bike_load_body(struct BikeContract *s,unsigned stream) { s->forward=(float)stream; s->cursor=stream; ++s->transitions; }

unsigned bike_query_body(struct BikeQueries *q,unsigned actor,unsigned type) {
    ++q->calls; if(actor!=q->actor) return 0;
    return type==0x2e ? q->component : type==0x2a ? q->physics : type==0x29 ? q->base : 0;
}
void bike_save_body(struct BikeContract *s,unsigned stream) { s->native_args[3]=stream; }

unsigned bike_on_event_body(struct BikeContract *s,unsigned *event,unsigned manager) {
    unsigned type=event[1]; s->native_args[3]=manager;
    if(type==64) return 0x7b40;
    if(type==13 || type==14 || type==29 || type==30) bike_event_body(s,type,*(float *)&event[2]);
    if(type==33) bike_event_body(s,29,0);
    if(type==34) bike_event_body(s,30,0);
    if(type==1 && (event[2]==10 || event[2]==13)) s->move_x=event[3] ? (event[2]==10 ? -50.f : 50.f) : 0.f;
    if(type==1 && event[2]==15) bike_event_body(s,event[3] ? 29 : 30,0);
    if(s->carry_scale) s->move_x*=s->carry_scale;
    if(s->after_event) s->after_event(s);
    return 0x7b00+type;
}
void bike_reset_one_body(struct BikeContract *s) { s->move_x=0; }
void bike_reset_all_body(struct BikeContract *s) { s->move_x=0; s->held=s->pressed=0; }
