#pragma once
/* Original synthetic state; no game layout or original bytes. */
struct AimIntentFixture {
    unsigned starting,active,finish,updates,unaims,forces,enabled,reloads,commits,magazine,reserve;
    unsigned in_update;
};

struct HeldIntentFixture {
    unsigned starting,active,enabled,can_start,latch,tutorial,starts,exits,animated,queries,hud_enabled,updates,close_gate_on_hud;
    unsigned reload_pending;
    int main_state,configured_reload;
    unsigned magazine,reserve,ammo_commits;
};
