#include "menu_observer_contract.h"
typedef unsigned long long U64;
struct MenuObserverFixture *obs_state;
extern U64 obs_unload(unsigned);
extern U64 obs_resource(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
extern U64 obs_select(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
extern U64 obs_width(unsigned,unsigned),obs_height(unsigned,unsigned);
extern U64 obs_rtt0(unsigned,unsigned,unsigned),obs_rtt1(unsigned,unsigned,unsigned);
static unsigned raw(const void *p) { return (unsigned)p; }
static U64 result(unsigned index) { return ((U64)(0xed000000u+index)<<32)|(0xac000000u+index); }
static void remember(unsigned index,const unsigned *args,unsigned count) {
    unsigned i; ++obs_state->counts[index];
    for(i=0;i<count;++i) obs_state->arguments[index][i]=args[i];
}
static void callback(unsigned at) {
    if(obs_state->callback_at==at && obs_state->callback) {
        unsigned fn=obs_state->callback; obs_state->callback_at=0;
        ((void (*)(void))fn)();
    }
}
#define LIFE(name,index) U64 name(unsigned p) { unsigned args[]={p}; remember(index,args,1); return result(index); }
LIFE(obs_c1_body,0)
LIFE(obs_c2_body,1)
LIFE(obs_d1_body,2)
U64 obs_d2_body(unsigned p) { unsigned args[]={p}; remember(3,args,1); obs_unload(p); return result(3); }
LIFE(obs_unload_body,4)
static void retain(unsigned index,U64 value) {
    obs_state->last_low[index]=(unsigned)value; obs_state->last_high[index]=(unsigned)(value>>32);
}
U64 obs_load_body(unsigned receiver,unsigned stream) {
    unsigned args[]={receiver,stream}; unsigned phase=*(unsigned *)(receiver+0x114);
    remember(5,args,2); callback(4);
    if(phase==10) {
        retain(0,obs_resource(raw(&obs_state->sprite_manager),raw(obs_state->input_name),0x11000000,0x22000001,0x33000000,0xffffffff));
        *(unsigned *)(obs_state->sprite+0xb8)=obs_state->nominal_width;
        *(unsigned *)(obs_state->sprite+0xbc)=obs_state->nominal_height;
        retain(2,obs_width(raw(&obs_state->sprite_manager),raw(obs_state->input_name)));
        retain(3,obs_height(raw(&obs_state->sprite_manager),raw(obs_state->input_name)));
    }
    if(phase==16 && !obs_state->suppress_rtt) {
        retain(4,obs_rtt0(raw(&obs_state->rtt[0]),obs_state->nominal_width,obs_state->nominal_height));
        retain(5,obs_rtt1(raw(&obs_state->rtt[1]),obs_state->nominal_width,obs_state->nominal_height));
    }
    return result(5);
}
U64 obs_resource_body(unsigned receiver,unsigned name,unsigned a,unsigned b,unsigned c,unsigned d) {
    unsigned args[]={receiver,name,a,b,c,d};
    unsigned text=0; float scale=1; int width=0,height=0;
    remember(6,args,6);
    retain(1,obs_select(receiver,name,raw(&text),raw(&scale),raw(&width),raw(&height),c));
    // Nonnegative signed EAX (resource index), distinctive EDX.
    return ((U64)0xed000006<<32)|0x12345678u;
}
U64 obs_select_body(unsigned receiver,unsigned name,unsigned text,unsigned scale,unsigned width,unsigned height,unsigned option) {
    unsigned args[]={receiver,name,text,scale,width,height,option}; remember(7,args,7);
    *(unsigned *)text=raw(obs_state->selected_name); *(float *)scale=1;
    *(int *)width=1720; *(int *)height=1280;
    callback(1); return result(7);
}
U64 obs_width_body(unsigned receiver,unsigned name) {
    unsigned args[]={receiver,name}; remember(8,args,2); callback(2);
    return ((U64)0xed000008<<32)|raw(obs_state->sprite);
}
U64 obs_height_body(unsigned receiver,unsigned name) {
    unsigned args[]={receiver,name}; remember(9,args,2);
    return ((U64)0xed000009<<32)|raw(obs_state->sprite);
}
U64 obs_rtt0_body(unsigned receiver,unsigned width,unsigned height) {
    unsigned args[]={receiver,width,height}; remember(10,args,3); callback(3); return result(10);
}
U64 obs_rtt1_body(unsigned receiver,unsigned width,unsigned height) {
    unsigned args[]={receiver,width,height}; remember(11,args,3); return result(11);
}

U64 obs_begin_body(unsigned receiver,unsigned clear) {
    unsigned args[]={receiver,clear}; remember(12,args,2); callback(5); return result(12);
}
U64 obs_rtt_d1_body(unsigned receiver) {
    unsigned args[]={receiver}; remember(13,args,1); callback(6); return result(13);
}
