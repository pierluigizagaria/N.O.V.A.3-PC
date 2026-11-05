#include "aim_intent_contract.h"
extern unsigned aim_fixture_publish(struct AimIntentFixture *);
extern unsigned aim_fixture_stop(struct AimIntentFixture *);
unsigned aim_fixture_update_body(struct AimIntentFixture *s,unsigned dt) {
    ++s->updates;
    s->in_update=1;
    if(s->starting && s->finish) aim_fixture_publish(s);
    s->in_update=0;
    return 0x5a000000u|dt;
}
unsigned aim_fixture_unaim_body(struct AimIntentFixture *s,unsigned animation) {
    if(s->in_update) __builtin_trap();
    if(s->active) { aim_fixture_stop(s); s->active=0; ++s->unaims; }
    return 0x6b000000u|animation;
}
unsigned aim_fixture_force_body(struct AimIntentFixture *s) {
    s->starting=s->active=0; ++s->forces; return 0x7c000001u;
}
unsigned aim_fixture_enable_body(struct AimIntentFixture *s,unsigned enabled) {
    s->enabled=enabled; return 0x8d000000u|enabled;
}
unsigned aim_fixture_reload_body(struct AimIntentFixture *s) {
    ++s->reloads;
    if(s->active) aim_fixture_unaim_body(s,1);
    return 0x9e000001u;
}
unsigned aim_fixture_commit(struct AimIntentFixture *s) {
    unsigned amount=10-s->magazine;
    if(amount>s->reserve) amount=s->reserve;
    s->magazine+=amount; s->reserve-=amount; ++s->commits;
    return 0xaf000001u;
}

extern unsigned held_can_start(struct HeldIntentFixture *,unsigned,unsigned);
extern unsigned held_exit(struct HeldIntentFixture *,unsigned,unsigned);
extern unsigned held_toggle(struct HeldIntentFixture *,unsigned,unsigned);
extern unsigned held_query_touch(struct HeldIntentFixture *,unsigned,unsigned);
unsigned held_query_touch_body(struct HeldIntentFixture *s) { (void)s; return 0; }
unsigned held_hud_body(struct HeldIntentFixture *s) {
    if(s->close_gate_on_hud) { s->close_gate_on_hud=0; s->can_start=0; }
    return (unsigned)s; /* Native supplied HUD object, queried before admission. */
}
unsigned held_can_start_body(struct HeldIntentFixture *s) {
    held_query_touch(s,0,0);
    ++s->queries; return 0xa5000000u|(s->can_start && !s->active);
}
unsigned held_exit_body(struct HeldIntentFixture *s,unsigned animated) {
    ++s->exits; s->animated=animated; s->active=s->starting=0; s->can_start=0;
    /* Ordinary animated UnAim selects an exit state, replacing any current
       main animation. Keeping reload unchanged here would hide interference. */
    if(animated) s->main_state=5;
    return 0xface0000u|animated;
}
unsigned held_switch(struct HeldIntentFixture *s) {
    if(s->active) return held_exit(s,1,0);
    if(held_can_start(s,0,0)&255u) { ++s->starts; s->starting=1; }
    return 0xbeef0000u;
}
unsigned held_update_body(struct HeldIntentFixture *s,unsigned button) {
    ++s->updates;
    if(!button) s->latch=1;
    else if(s->latch) { held_toggle(s,0,0); ++s->tutorial; s->latch=0; }
    return 0xdeed0000u;
}
unsigned held_interrupt_body(struct HeldIntentFixture *s) {
    s->starting=s->active=0; return 0xdead0000u;
}
unsigned held_enable_body(struct HeldIntentFixture *s,unsigned enabled) {
    s->enabled=enabled; return enabled;
}

/* Original native reload model. Indices are synthetic, not game state enums. */
extern unsigned held_reload_exit(struct HeldIntentFixture *,unsigned,unsigned);
unsigned held_main_state_body(struct HeldIntentFixture *s) { return (unsigned)s->main_state; }
unsigned held_reload_begin_body(struct HeldIntentFixture *s,unsigned accepted) {
    if(!accepted) return 0xabcd0000u; /* full magazine/no reserve/native refusal */
    if(s->active) { held_reload_exit(s,1,0); s->reload_pending=1; }
    else s->main_state=s->configured_reload;
    s->can_start=1; /* The generic aim gate may still permit the transition gap. */
    return 0xabcd0001u;
}
unsigned held_reload_publish_body(struct HeldIntentFixture *s) {
    s->main_state=s->configured_reload; return 0;
}
extern unsigned held_reload_restore(struct HeldIntentFixture *,unsigned,unsigned);
unsigned held_reload_restore_body(struct HeldIntentFixture *s) {
    ++s->starts; s->starting=1; s->main_state=9; return 0xdecafbad;
}
unsigned held_reload_commit_body(struct HeldIntentFixture *s) {
    unsigned amount=10-s->magazine;
    if(amount>s->reserve) amount=s->reserve;
    s->magazine+=amount; s->reserve-=amount; ++s->ammo_commits;
    if(s->reload_pending) held_reload_restore(s,0,0);
    s->reload_pending=0; /* Native auto-restore CALL precedes this clear. */
    return amount;
}
unsigned held_reload_end_body(struct HeldIntentFixture *s) {
    s->reload_pending=0; s->main_state=3; return 0;
}
