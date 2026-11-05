// UNEXECUTED host-only Release checks. No ELF/SDL/device/guest callbacks.
#include "menu/menu_ack_gate.h"
#include <cstdio>
using namespace nova::diagnostic::t09h;
static int failures=0;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"T09g failure line %d: %s\n",__LINE__,#x);++failures; } } while(false)
static Snapshot ready() {
 Snapshot s{};s.frame=534;s.state=State::splash;
 s.valid=s.player_absent=s.gameplay_absent=s.underlying_menu=true;
 s.moga_clear=s.acknowledgement_clear=s.viewport_stable=s.no_other_input=true;
 s.cpu_complete=s.two_targets_registered=s.targets_healthy=true;
 s.splash_identity=7;s.manager_identity=9;s.generation=1;return s;
}
int main() {
 {Gate g;auto s=ready();s.state=State::load_menu;CHECK(g.observe(s).action==Action::wait);
  s.state=State::splash;CHECK(g.observe(s).action==Action::dispatch_balanced_pair);
  CHECK(g.delivered_pair(true));++s.frame;s.state=State::menu;
  CHECK(g.observe(s).action==Action::wait); // State alone is not draw evidence.
  ++s.frame;s.any_begin=true;s.any_return_with_events=true;
  CHECK(g.observe(s).action==Action::stop_observed);
  CHECK(g.observe(s).action==Action::stop_observed);}
 {Gate g;auto s=ready();s.moga_clear=false;CHECK(g.observe(s).reason==Reason::unsupported_splash);}
 {Gate g;auto s=ready();s.underlying_menu=false;CHECK(g.observe(s).action==Action::stop_failed);}
 {Gate g;auto s=ready();s.player_absent=false;CHECK(g.observe(s).reason==Reason::unsafe_snapshot);}
 {Gate g;auto s=ready();g.observe(s);CHECK(g.observe(s).reason==Reason::delivery_failed);}
 {Gate g;auto s=ready();g.observe(s);CHECK(!g.delivered_pair(false));
  CHECK(g.observe(s).reason==Reason::delivery_failed);}
 {Gate g;auto s=ready();g.observe(s);g.delivered_pair(true);++s.frame;++s.generation;
  CHECK(g.observe(s).reason==Reason::owner_changed);}
 {Gate g;auto s=ready();g.observe(s);g.delivered_pair(true);s.frame+=120;s.state=State::menu;
  s.any_begin=true;CHECK(g.observe(s).reason==Reason::draw_timeout);}
 {Gate g;auto s=ready();s.frame=1200;s.state=State::load_menu;
  CHECK(g.observe(s).reason==Reason::startup_timeout);}
 {Gate g;auto s=ready();g.observe(s);g.delivered_pair(true);++s.frame;s.state=State::other;
  CHECK(g.observe(s).reason==Reason::unexpected_state);}
 return failures?1:0;
}
