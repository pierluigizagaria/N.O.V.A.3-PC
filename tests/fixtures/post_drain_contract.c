#include "post_drain_contract.h"
struct PostDrainWorld post_drain_world;
unsigned event_singleton __asm__("_ZN18GlobalEventManager9SingletonE");
unsigned move_table[4] __asm__("_ZTV11EvStickMove");
unsigned release_table[4] __asm__("_ZTV15EvStickReleased");
unsigned player_table[32] __asm__("_ZTV15PlayerComponent");
unsigned ingame_table[4] __asm__("_ZTV9GS_InGame");
static unsigned get(unsigned p,unsigned off) { return *(unsigned *)(p+off); }
static float scalar(unsigned p,unsigned off) { return *(float *)(p+off); }
static void put(unsigned p,unsigned off,unsigned value) { *(unsigned *)(p+off)=value; }
static void number(unsigned p,unsigned off,float value) { *(float *)(p+off)=value; }
static void callback(unsigned address,unsigned arg) { if(address) ((void (*)(unsigned))address)(arg); }
unsigned fixture_get_level(void) __asm__("_ZN6CLevel8GetLevelEv");
unsigned fixture_get_level(void) { return post_drain_world.level; }
unsigned fixture_get_player(unsigned level) __asm__("_ZN6CLevel18GetPlayerComponentEv");
unsigned fixture_get_player(unsigned level) {
    unsigned p=post_drain_world.player; (void)level; ++post_drain_world.acquisitions;
    callback(post_drain_world.getter_callback,p); return p;
}
unsigned fixture_get_app(void) __asm__("_ZN11Application11GetInstanceEv");
unsigned fixture_get_app(void) { return post_drain_world.app; }
unsigned fixture_get_state(unsigned app) __asm__("_ZN12gxStateStack12CurrentStateEv");
unsigned fixture_get_state(unsigned app) { (void)app; return post_drain_world.state; }
unsigned fixture_dead(unsigned actor) __asm__("_ZNK11CGameObject6IsDeadEv");
unsigned fixture_dead(unsigned actor) { return get(actor,0x118); }
unsigned fixture_cinematic(unsigned player) __asm__("_ZN15PlayerComponent19IsWatchingCinematicEv");
unsigned fixture_cinematic(unsigned player) { return get(player,0x130)==3; }
#define ROLE(name,symbol) unsigned name(unsigned p) __asm__(symbol); unsigned name(unsigned p) { (void)p; return 0; }
ROLE(fixture_mech,"_ZN6CLevel22GetPlayerMechComponentEv")
ROLE(fixture_gun,"_ZN6CLevel28GetPlayerMachineGunComponentEv")
ROLE(fixture_jeep,"_ZN6CLevel22GetPlayerJeepComponentEv")
ROLE(fixture_bike,"_ZN6CLevel22GetPlayerBikeComponentEv")
unsigned fixture_move_control(unsigned scheme) __asm__("_ZN14ControlScheme414GetMoveControlEv");
unsigned fixture_move_control(unsigned scheme) { return get(scheme,0xc); }
unsigned fixture_look_control(unsigned scheme) __asm__("_ZN14ControlScheme416GetRotateControlEv");
unsigned fixture_look_control(unsigned scheme) { return get(scheme,0x18); }
unsigned fixture_lifetime_body(unsigned p,unsigned actor) {
    struct PostDrainPlayer *s=(struct PostDrainPlayer *)p;
    ++s->lifetimes;
    if(actor) { put(p,4,actor); put(p,0x100,actor+0x68); put(p,0x104,actor+0x60); }
    number(p,0x48,0); number(p,0x4c,0); number(p,0x64,0);
    number(p,0x1c,0); number(p,0x24,0);
    return 0xa5000001u;
}
unsigned fixture_enable_body(unsigned p,unsigned value) {
    fixture_lifetime_body(p,0); *(unsigned char *)(p+0xd)=(unsigned char)value; return 0xa5000002u;
}
void fixture_reset(unsigned p) {
    callback(post_drain_world.reset_observer,p+0x10);
    number(p,0x48,0); number(p,0x4c,0); number(p,0x64,0);
    number(p,0x1c,0); number(p,0x24,0);
}
/* Native arithmetic subset, configured with template8=2, move3/5 and level
   scale .5. Yaw .02 and relative pitch cadence follow the retained consumer. */
unsigned fixture_event_body(unsigned control,const struct PostDrainEvent *event,unsigned manager) {
    unsigned p=control-0x10; struct PostDrainPlayer *s=(struct PostDrainPlayer *)p;
    (void)manager;
    if(s->poison) { ++s->obsolete; return 0xdead0001u; }
    if(post_drain_world.event_observer)
        ((void (*)(unsigned,unsigned))post_drain_world.event_observer)(control,(unsigned)event);
    ++s->events; s->last_x=event->x; s->last_y=event->y;
    if(post_drain_world.native_event_entry) {
        ((void (*)(unsigned,const struct PostDrainEvent *,unsigned))post_drain_world.native_event_entry)(control,event,manager);
        return 0xe9000000u|event->type;
    }
    if(event->type==9) {
        number(p,0x48,event->x*2.f); number(p,0x4c,event->x*2.f);
        number(p,0x64,-event->y*2.f);
    }
    if(event->type==10 || event->type==58) {
        number(p,0x48,0); number(p,0x4c,0); number(p,0x64,0);
    }
    if(event->type==13) { number(p,0x1c,event->x*3.f*.5f); number(p,0x24,-event->y*5.f*.5f); }
    if(event->type==14) { number(p,0x1c,0); number(p,0x24,0); }
    return 0xe9000000u|event->type;
}
unsigned fixture_raise_sync(unsigned manager,const struct PostDrainEvent *event) __asm__("_ZN12EventManager9raiseSyncERK6IEvent");
unsigned fixture_raise_sync(unsigned manager,const struct PostDrainEvent *event) {
    (void)manager; ++post_drain_world.queued;
    if(post_drain_world.queue_count==16) return 0xbad00010u;
    post_drain_world.queue[post_drain_world.queue_count++]=*event;
    return 0x51554555u;
}
static unsigned send(const struct PostDrainEvent *event) {
    unsigned p=post_drain_world.player;
    unsigned result=p ? fixture_event_body(p+0x10,event,post_drain_world.manager) : 0;
    /* A later subscriber may retire/reconstruct the just-used player. sendEvent
       retains an iterator, NOT a player receiver, after its virtual callback. */
    if(event->type==post_drain_world.callback_type) callback(post_drain_world.event_callback,p);
    return result;
}
unsigned fixture_raise_async(unsigned manager,const struct PostDrainEvent *event) __asm__("_ZN12EventManager10raiseAsyncERK6IEvent");
unsigned fixture_raise_async(unsigned manager,const struct PostDrainEvent *event) {
    ++post_drain_world.immediate;
    if(post_drain_world.queue_host) return fixture_raise_sync(manager,event);
    return send(event);
}
unsigned fixture_update_events(unsigned manager,unsigned dt) __asm__("_ZN12EventManager12UpdateEventsEi");
unsigned fixture_update_events(unsigned manager,unsigned dt) {
    unsigned i,count=post_drain_world.queue_count; (void)manager;
    ++post_drain_world.drains; post_drain_world.drain_dt=dt;
    for(i=0;i<count;++i) send(&post_drain_world.queue[i]);
    post_drain_world.queue_count=0;
    return 0xd0000000u|count;
}
unsigned fixture_consume_body(unsigned p,unsigned dt) {
    struct PostDrainPlayer *s=(struct PostDrainPlayer *)p;
    unsigned actor;
    if(s->poison) { ++s->obsolete; return 0xdead0021u; }
    ++s->consumers; s->last_dt=dt; actor=get(p,4);
    if(post_drain_world.native_consumer_entry) {
        ((void (*)(unsigned,unsigned))post_drain_world.native_consumer_entry)(p,dt);
        return 0xcafe0021u;
    }
    if(*(unsigned char *)(p+0xd) && !*(unsigned char *)(p+0x139)) {
        if(*(unsigned char *)(p+0x58)) {
            number(actor,0x68,scalar(p,0x54)); *(unsigned char *)(p+0x58)=0;
            number(actor,0x60,scalar(actor,0x60)+scalar(p,0x60));
        } else {
            number(actor,0x68,scalar(actor,0x68)-scalar(p,0x48)*(float)(int)dt*.02f);
            number(actor,0x60,scalar(actor,0x60)+scalar(p,0x64));
        }
    }
    return 0xcafe0021u;
}
unsigned fixture_look_at(unsigned p,unsigned dt) {
    struct PostDrainPlayer *s=(struct PostDrainPlayer *)p; (void)dt;
    ++s->look_calls; if(s->poison) ++s->obsolete; return get(p,0x1b0);
}
unsigned fixture_player_caller(unsigned p,unsigned dt) {
    struct PostDrainPlayer *s=(struct PostDrainPlayer *)p;
    if(post_drain_world.native_caller_entry)
        return ((unsigned (*)(unsigned,unsigned))post_drain_world.native_caller_entry)(p,dt);
    fixture_consume_body(p,dt);
    fixture_look_at(p,dt); /* original 0x21ca13 */
    if(s->poison) ++s->obsolete;
    { volatile unsigned value=get(p,0x1b0); (void)value; } /* original 0x21ca18 */
    if(s->poison) ++s->obsolete;
    put(p,0x2f0,get(p,0x2f0)-dt); /* original 0x21ca1e */
    return 0xc0110001u;
}
unsigned fixture_after_drain(unsigned old_player,unsigned dt,unsigned result) {
    unsigned p;
    post_drain_world.forwarded_result=result;
    ++post_drain_world.effects_calls; ++post_drain_world.mp_calls;
    callback(post_drain_world.after_drain_callback,post_drain_world.player);
    ++post_drain_world.state_calls;
    /* Ordinary UpdateApp reacquires state, level/zone then the current receiver.
       The stale_caller omission deliberately retains the pre-drain old borrow. */
    p=post_drain_world.stale_caller ? old_player : fixture_get_player(fixture_get_level());
    if(p) return fixture_player_caller(p,dt);
    return 0;
}
unsigned fixture_before_drain(void) { return post_drain_world.player; }

unsigned fixture_inactive(unsigned p) { (void)p; return 0; }
