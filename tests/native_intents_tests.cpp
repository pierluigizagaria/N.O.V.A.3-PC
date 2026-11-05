#include "input/native_intents.h"
#include <cstdio>
#include <stdexcept>
using nova::input::NativeAimIntent;
namespace {
void check(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
// Original synthetic normal-input contract: a native toggle latch plus native
// current flags/gates. This is not the installed ELF/ABI verification.
struct Consumer {
    NativeAimIntent intent;
    bool latch=true,enabled=true,hud_enabled=true,can_start=false,starting=false,active=false,exit_animation=false;
    unsigned attempts=0,starts=0,exits=0,tutorial=0;
    void reconcile() {
        ++attempts;
        if(active) {
            if(enabled && intent.permit_exit()) {
                ++exits; active=false; starting=false; exit_animation=true;
                // Ordinary native UnAim(true), never ForceUnAim or false.
            }
        } else if(intent.permit_start(can_start,starting,enabled,hud_enabled)) {
            ++starts; starting=true;
        }
    }
    void update() {
        bool attempted=false;
        if(!intent.button()) latch=true;
        else if(latch) { reconcile(); attempted=true; ++tutorial; latch=false; }
        if(!attempted) reconcile();
    }
};
}
int main() {
    try {
        // Counterexample to C03: native polls only the host's active pulse
        // frames. It never sees an inactive rearm, despite later permission.
        bool old_button=false,old_latch=true; unsigned old_calls=0;
        for(unsigned host=0;host<8;++host) {
            old_button=!old_button;
            if(host%2==0) {
                if(!old_button) old_latch=true;
                else if(old_latch) { ++old_calls; old_latch=false; }
            }
        }
        check(old_calls==1,"old alternating button loses retry at different consumer cadence");
        Consumer hud; hud.intent.sample(true); hud.can_start=true; hud.hud_enabled=false;
        hud.update(); check(hud.starts==0 && hud.intent.wanted(),"disabled aim HUD blocks tail start while retaining held desire");
        hud.hud_enabled=true; hud.update(); check(hud.starts==1,"native HUD reenable permits the same held desire");
        hud.intent.sample(false); hud.starting=false; hud.active=true; hud.hud_enabled=false;
        hud.update(); check(hud.exits==1,"HUD start denial cannot block ordinary release");
        Consumer c; c.intent.sample(true);
        // Zero native updates for several host frames must not expire intent.
        for(unsigned host=0;host<100;++host) c.intent.sample(true);
        c.update(); check(c.starts==0 && c.tutorial==1,"first refused native press retains tutorial");
        c.can_start=true; c.update(); c.update();
        check(c.starts==1 && c.attempts==3 && c.tutorial==1,"two native updates converge once without fake rearm or repeat entry");
        check(c.starting && !c.active,"submission is not animation completion or acceptance ACK");
        // Native interrupt clears its own flags directly, without observers.
        c.starting=false; c.active=false; c.update();
        check(c.starts==2,"direct native interruption respects latest still-held desire");
        c.starting=false; c.enabled=false; c.update();
        check(c.starts==2 && c.intent.wanted(),"temporary weapon permission preserves physical desired aim");
        c.enabled=true; c.update(); check(c.starts==3,"native reenable needs no new physical press");
        c.intent.sample(false); c.update(); check(c.exits==0,"release during native entry waits without forcing flags");
        c.starting=false; c.active=true; c.update(); c.update();
        check(c.exits==1 && c.exit_animation,"ordinary release submits one native animated exit");
        c.can_start=false; c.intent.sample(true); c.update();
        check(c.starts==3,"native exit gate owns interruptibility; no host animation reset");
        c.exit_animation=false; c.can_start=true; c.update();
        check(c.starts==4,"latest held desire reconciles when native exit gate opens");
        c.intent.offer_reload(true); c.intent.offer_reload(true);
        check(!c.intent.permit_start(true,false,true,true),"one-shot reload takes native handoff priority without erasing held aim");
        check(c.intent.take_reload() && !c.intent.take_reload(),"coalesced native reload handoff is once-only without wall time");
        c.intent.cancel_input(); c.starting=false; c.active=false; c.update();
        check(!c.intent.wanted() && c.starts==4,"focus or panel cancellation cannot resume stale aim");
        c.intent.sample(true); c.intent.block_until_release(); c.intent.sample(true); c.update();
        check(!c.intent.wanted(),"new ownership requires rearm after explicit context cancel");
        c.intent.sample(false); c.intent.sample(true); c.update(); check(c.starts==5,"fresh physical press rearms after context cancel");
        NativeAimIntent reload_intent; reload_intent.sample(true);
        check(nova::input::native_reload_blocks_aim(true,3,7),"native pending reload blocks despite generic aim eligibility");
        check(nova::input::native_reload_blocks_aim(false,7,7),"configured native reload remains busy after pending byte clears");
        check(!nova::input::native_reload_blocks_aim(false,3,7),"refused R alone cannot manufacture reload state");
        check(!reload_intent.permit_start(true,false,true,true,true) && !reload_intent.permit_exit() && reload_intent.wanted(),
              "native reload suspends new starts without injecting a second exit or erasing held desire");
        check(reload_intent.permit_start(true,false,true,true,false),"held ADS resumes on actual native reload-state exit");
        reload_intent.sample(false);
        check(!reload_intent.permit_start(true,false,true,true,false),"released aim cannot resume after reload ends");
        reload_intent.sample(true); reload_intent.cancel_input();
        check(!reload_intent.permit_start(true,false,true,true,false),"context cancellation retires desired aim across reload");
        std::puts("native held intent/cadence contract passed");
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
