// UNEXECUTED protocol fixtures. No guest code, native predicate, hook or GPU.
// The sink model tests the shared contract and existing production lens math.
// It does not test NativeWorldFov's real matrix/cache adapter; see handoff.md.
#include "camera/viewmodel_eligibility.h"
#include "camera/world_fov.h"
#include "fixtures/viewmodel_eligibility_fixture.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {
using namespace nova::camera;
namespace ve=nova::camera::viewmodel_eligibility;
using ve::fixture::Producer;
constexpr double pi=std::numbers::pi;
void check(bool ok,const char *what) { if(!ok) throw std::runtime_error(what); }
void near(double actual,double expected,const char *what) {
    if(!std::isfinite(actual) || std::abs(actual-expected)>1.e-5) throw std::runtime_error(what);
}
struct Geometry {
    std::array<float,16> projection{};
    float near_x=0,far_x=0;
    bool operator==(const Geometry &) const = default;
};
Geometry original_builder(float raw) {
    Geometry g;
    g.projection[0]=1.f/std::tan(raw*.5f)/float(reference_aspect);
    g.projection[5]=1.f/std::tan(raw*.5f);
    g.projection[10]=-101.f/99.f; g.projection[11]=-1.f;
    g.projection[14]=-200.f/99.f;
    g.near_x=1.f/g.projection[0]; g.far_x=100.f/g.projection[0];
    return g;
}
struct Surface {
    uint64_t lifetime=3;
    float raw=.9f, reference=.9f;
    Geometry cpu=original_builder(raw),driver=cpu;
    float driver_view=7;
    bool dirty=false,owned_custom=false;
    unsigned builds=0,sinks=0;
};
struct SinkObservation { Geometry cpu,driver; float raw,view; ve::WeaponPolicy policy; };
struct Pipeline {
    // A copied preference, not a disk/config writer. Eligibility never edits it.
    std::optional<float> requested;
    uint64_t preference_revision=0,clock=0,authority_revision=1;
    ve::OwnerEpochs owners{1,2,5,3,4};
    Surface surface;
    const float world_output=3.125f; // Separately scoped world policy sentinel.
    bool omit_retirement=false;
    void request(std::optional<float> value) {
        check(!value || (std::isfinite(*value) && *value>=60 && *value<=104),"fixture setting domain");
        requested=value; ++preference_revision;
    }
    ve::Boundary next() { return {++clock,authority_revision,owners}; }
    SinkObservation render(const ve::Decision &decision,const ve::Boundary &current) {
        check(current.owners.camera==surface.lifetime,"fixture supplies the live receiver, not a saved pointer");
        const auto raw_bits=std::bit_cast<uint32_t>(surface.raw);
        const auto policy=ve::weapon_policy(decision,current,requested.has_value());
        const auto angle=policy==ve::WeaponPolicy::custom ?
            weapon_vertical_fov({surface.raw,surface.reference},requested) : std::nullopt;
        check(policy!=ve::WeaponPolicy::custom || angle.has_value(),"proved fixture lens is valid");
        // Model the required current-receiver retirement before the FIRST sink.
        // The omission switch is a future negative control, not a native patch.
        if(angle || surface.dirty || (surface.owned_custom && !omit_retirement)) {
            surface.cpu=original_builder(angle.value_or(surface.raw));
            ++surface.builds; surface.owned_custom=angle.has_value(); surface.dirty=false;
        }
        surface.driver=surface.cpu; ++surface.sinks; // Direct publication, no lazy getter.
        check(std::bit_cast<uint32_t>(surface.raw)==raw_bits,"raw lens was modified");
        return {surface.cpu,surface.driver,surface.raw,surface.driver_view,policy};
    }
    SinkObservation ordinary() {
        const auto current=next(); return render(Producer::ordinary(current),current);
    }
    SinkObservation native(ve::Reason reason=ve::Reason::native_non_fps) {
        const auto current=next(); return render(ve::Decision::native(reason),current);
    }
};
void native_sink(const SinkObservation &o,const char *message) {
    check(o.policy==ve::WeaponPolicy::native,"ineligible policy must remain explicit Native weapon");
    const auto expected=original_builder(o.raw);
    check(o.cpu==expected,message);
    check(o.driver==expected,"first ineligible DRIVER must contain full native projection");
    near(o.cpu.near_x*o.cpu.projection[0],1,"native near ray matches matrix");
    near(o.cpu.far_x*o.cpu.projection[0],100,"native far bound matches matrix");
}
void transitions(bool omit) {
    Pipeline p; p.omit_retirement=omit;
    native_sink(p.ordinary(),"initial Native default");
    p.request(104.f); const auto revision=p.preference_revision;
    auto custom=p.ordinary();
    check(custom.policy==ve::WeaponPolicy::custom,"ordinary FPS applies requested Custom");
    check(custom.cpu!=original_builder(custom.raw),"fixture starts with an actual custom matrix");
    const auto builds=p.surface.builds;
    // Same owner, raw bits and native clean cache on death: no setter dirties it.
    native_sink(p.native(),"first ineligible CPU must retire a clean custom stamp");
    check(p.surface.builds==builds+1,"first ineligible sink rebuilds immediately");
    check(p.requested==104.f && p.preference_revision==revision,"death preserves requested settings");
    const auto restored=p.ordinary();
    check(restored.cpu==custom.cpu && restored.driver==custom.driver,"respawn restores Custom without a settings rewrite");
    for(auto reason:{ve::Reason::native_non_fps,ve::Reason::unproved,ve::Reason::lifetime_changed}) {
        native_sink(p.native(reason),"cinematic/non-FPS/unknown first-frame fallback");
        check(p.ordinary().cpu==custom.cpu,"legitimate FPS return restores Custom");
    }
    check(p.preference_revision==revision && p.world_output==3.125f,"eligibility never changes preference or world policy");
    p.request(std::nullopt); native_sink(p.ordinary(),"Reset is original Native");
    check(!p.requested,"Reset clears Custom request only on explicit Reset");
}
void ownership_and_freshness() {
    Pipeline p; p.request(100.f);
    const auto first=p.next(); const auto old=Producer::ordinary(first);
    (void)p.render(old,first);
    const auto next=p.next();
    native_sink(p.render(old,next),"prior consumption permit must not authorize a new render");
    for(unsigned member=0;member<5;++member) {
        (void)p.ordinary();
        const auto current=p.next(); auto mismatched=current;
        auto *epoch=member==0?&mismatched.owners.local_root:member==1?&mismatched.owners.actor:
            member==2?&mismatched.owners.health:member==3?&mismatched.owners.camera:&mismatched.owners.weapon;
        ++*epoch;
        native_sink(p.render(Producer::ordinary(mismatched),current),"owner lifetime mismatch retires current receiver output");
        *epoch=0;
        check(!Producer::ordinary(mismatched).permits(mismatched),"missing owner never issues a positive permit");
    }
    // Late death after initial authorization, with unchanged raw/owner/serial.
    const auto live=p.next(); const auto before_mutation=Producer::ordinary(live);
    (void)p.render(before_mutation,live);
    auto revoked=live; revoked.authority_revision=++p.authority_revision;
    native_sink(p.render(before_mutation,revoked),"same-consumption native mutation revokes the prior decision");
    check(p.requested==100.f,"native mutation revocation retains request");
    (void)p.ordinary(); p.owners.health=0;
    native_sink(p.native(ve::Reason::owner_lost),"absent health is unknown, never positive alive");
    p.owners.health=12;
    (void)p.ordinary(); p.owners.actor=0;
    native_sink(p.native(ve::Reason::owner_lost),"actor loss keeps cleanup authority on the supplied camera");
    p.owners.actor=9; check(p.ordinary().policy==ve::WeaponPolicy::custom,"new actor lifetime can regain eligibility");
    (void)p.ordinary(); p.owners.weapon=0;
    native_sink(p.native(ve::Reason::owner_lost),"weapon loss retires Custom without clearing request");
    p.owners.weapon=10;
    // No dereference or restore of an old camera. The new receiver is native.
    const auto retired=p.surface;
    p.owners.camera=11; p.surface=Surface{}; p.surface.lifetime=11;
    native_sink(p.native(ve::Reason::owner_lost),"new camera starts Native until independently proved");
    check(retired.owned_custom==false,"previous weapon loss already retired old output");
    check(p.ordinary().policy==ve::WeaponPolicy::custom,"replacement camera obtains independent proof");
    const auto serial=p.next();
    check(!ve::Decision::native(ve::Reason::ordinary_fps).permits(serial),"diagnostic reason cannot manufacture authority");
    auto zero=serial; zero.consumption=0;
    check(!Producer::ordinary(zero).permits(zero),"missing consumption serial is unproved");
    zero=serial; zero.authority_revision=0;
    check(!Producer::ordinary(zero).permits(zero),"missing native revocation revision is unproved");
}
void optics_and_nested_consumers() {
    Pipeline p; p.request(104.f);
    // Forward/reverse native samples model ADS/run/jump optical evolution.
    // These labels are not native state evidence; no input/timer/animation guard.
    const auto revision=p.preference_revision;
    for(float raw:{.9f,.8f,.7f,.8f,.9f,1.f,.9f}) {
        p.surface.raw=raw; p.surface.reference=raw; p.surface.dirty=true;
        const auto o=p.ordinary();
        const double native_extent=reference_aspect*std::tan((90./reference_aspect)*pi/360.);
        const double scale=std::tan(104.*pi/360.)/native_extent;
        near(o.cpu.projection[0],1./(scale*std::tan(double(raw)*.5)*reference_aspect),"Custom preserves native optical trajectory");
        check(o.cpu==o.driver,"first CPU and driver see the same projected optics");
    }
    const auto parent_key=p.next(); const auto parent=Producer::ordinary(parent_key);
    const auto expected=p.render(parent,parent_key);
    native_sink(p.native(),"nested non-FPS child sees Native before first sink");
    const auto parent_return=p.next();
    native_sink(p.render(parent,parent_return),"return cannot revive a saved parent permit");
    const auto fresh=p.next(); const auto resumed=p.render(Producer::ordinary(fresh),fresh);
    check(resumed.cpu==expected.cpu && resumed.driver==expected.driver,"fresh parent proof restores Custom after child");
    check(p.requested==104.f && p.preference_revision==revision,"nested/trajectory consumption keeps saved request");
}
}
int main(int argc,char **argv) {
    try {
        const bool omit=argc==2 && std::string_view(argv[1])=="--omit-retirement";
        check(argc==1 || omit,"unexpected fixture argument");
        transitions(omit); ownership_and_freshness(); optics_and_nested_consumers();
        std::puts("viewmodel eligibility protocol fixtures passed; native integration remains unproved");
        return 0;
    } catch(const std::exception &error) {
        std::fprintf(stderr,"%s\n",error.what()); return 1;
    }
}
