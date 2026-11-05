#include "mounted_machinegun_contract.h"
/* Original bounded trigonometric model; no extra imports in shared input.so. */
static float sine(float x) {
    float term,sum; unsigned i;
    while(x>3.141592654f) x-=6.283185307f;
    while(x< -3.141592654f) x+=6.283185307f;
    term=sum=x;
    for(i=1;i<8;++i) { term*= -x*x/((float)(2*i)*(float)(2*i+1)); sum+=term; }
    return sum;
}
static float cosine(float x) { return sine(x+1.570796327f); }
extern void machinegun_consume(struct MachineGunContract *,unsigned);
extern void machinegun_update_body(struct MachineGunContract *,unsigned);
unsigned mounted_query_body(struct MountedQueries *s,unsigned object,unsigned type) {
    unsigned i; ++s->calls;
    for(i=0;i<s->count;++i) if(s->entries[i].object==object && s->entries[i].type==type) return s->entries[i].component;
    return 0;
}
/* Native AI is a separate phase. Do not flush this from the input consumer. */
void mounted_ai_body(struct MountedMachineGun *s) {
    s->node_yaw=s->gun.yaw; s->node_pitch=s->gun.pitch; ++s->ai_phases;
}
static void compose(struct MountedMachineGun *s,float yaw,float pitch,float *out) {
    const float r=.017453292519943295f;
    const float y=(s->parent_yaw+yaw)*r,p=(pitch+s->tilt)*r,b=s->bank*r;
    const float x=sine(y)*cosine(p),z=cosine(y)*cosine(p),v=-sine(p);
    out[0]=x*cosine(b)-v*sine(b); out[1]=x*sine(b)+v*cosine(b); out[2]=z;
}
void mounted_update_body(struct MountedMachineGun *s,unsigned dt) {
    unsigned i; ++s->updates; s->dt=dt;
    if(s->gun.exiting) {
        if(s->gun.look.pitch_delta) {
            const float p=s->gun.pitch+s->gun.look.pitch_delta;
            if((s->gun.look.pitch_delta>0 && p>=0) || (s->gun.look.pitch_delta<0 && p<=0)) {
                s->gun.pitch=0; s->gun.look.pitch_delta=0;
            } else s->gun.pitch=p;
        }
    } else {
        machinegun_consume(&s->gun,dt);
        s->delay-=(int)dt;
        if(s->duration>0 && s->delay<=0) {
            s->duration-=(int)dt;
            /* Bounded interpolation endpoint; native target geometry stays out of scope. */
            s->gun.yaw=s->look_yaw; s->gun.pitch=s->look_pitch;
        }
        /* Parent-bank camera work follows MG input, using its native cached node. */
        ++s->bank_phases; s->camera_tilt_delta=s->cached_tilt-s->tilt; s->cached_tilt=s->tilt;
        compose(s,s->node_yaw,s->node_pitch,s->world_direction);
        compose(s,s->node_yaw,s->node_pitch+s->camera_tilt_delta,s->camera_direction);
        machinegun_update_body(&s->gun,dt);
    }
    ++s->player_phases;
    for(i=0;i<3;++i) s->world_position[i]=s->translation[i]+s->world_direction[i];
}
