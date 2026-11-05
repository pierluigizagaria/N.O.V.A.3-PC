// O08g authored only. Sole integrator may run Release after scoped integration.
#ifndef NOVA_O08G_RELEASE
#error Release-only focused verification
#endif
#include "movement_wobble/movement_wobble.h"
#include "movement_wobble/integration_gate.h"
#include "platform/fov_config.h"
#include <cstdio>
#include <stdexcept>
using nova::movement_wobble::Strength;
static void check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
int main() {
    try {
        using namespace nova::movement_wobble;
        static_assert(kDefaultPercent==100 && kResetPercent==100 && kMaximumPercent==100);
        static_assert(kNativeAdapterConnected,"O08 enabled policy requires the connected native adapter");
        check(kControlAvailable,"connected movement control is available");
        Strength strength;check(strength.percent()==100,"original default");
        strength=Strength::from_percent(37);strength.reset();check(strength.percent()==100,"original reset");
        check(Strength::from_percent(-1).percent()==0 && Strength::from_percent(101).percent()==100,"reduction-only bounds");
        RequestedStrength request(Strength::from_percent(37));check(request.requested().percent()==37,"explicit host setting preserved");
        request.set(Strength{});check(request.requested().percent()==100,"host Reset preference");
        for(auto text:{"-1","101","50x","nan","0.5",""}) check(!parse_strength(text),"invalid serialized value rejected");
        check(parse_strength("0")->percent()==0 && parse_strength("37")->percent()==37 && parse_strength("100")->percent()==100,"strict normalized serialized percent");
        const std::string original="# keep\r\nworld_fov = native\nweapon_fov = 104\r\n  movement_wobble = 37  \r\nmovement_wobble=80\nhosts.example=127.0.0.1";
        const auto merged=nova::platform::merge_movement_wobble_config(original,Strength::from_percent(50));
        check(merged=="# keep\r\nworld_fov = native\nweapon_fov = 104\r\n  movement_wobble = 50  \r\nmovement_wobble=50\nhosts.example=127.0.0.1","update every own key and preserve unrelated bytes/line endings");
        check(nova::platform::merge_movement_wobble_config("world_fov = native",Strength{})=="world_fov = native\r\nmovement_wobble = 100\r\n","missing key persists native default only on explicit save");
        const auto reset=nova::platform::merge_movement_wobble_config(merged,Strength{});
        check(reset.find("movement_wobble = 100")!=std::string::npos && reset.find("weapon_fov = 104")!=std::string::npos,"reset own value without FOV replacement");
        const auto fov=nova::platform::merge_fov_config(merged,{});
        check(fov.find("movement_wobble = 50")!=std::string::npos && fov.find("world_fov = native")!=std::string::npos,"ordinary FOV save recognizes and preserves explicit movement setting");
        bool rejected=false;try { (void)nova::platform::merge_movement_wobble_config("movement_wobble = 101\n",Strength{}); }
        catch(const std::runtime_error&) { rejected=true; }check(rejected,"invalid current key cannot silently corrupt settings");
        std::puts("PASS: authored inactive movement preference/config policy; no native activation or profile IO");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
