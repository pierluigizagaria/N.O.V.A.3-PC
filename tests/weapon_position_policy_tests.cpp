// Authored source only: pure math/settings policy, never native eligibility proof.
#include "weapon_placement/supplied_frame_policy.h"
#include "platform/guest_extent.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace nova::weapon_placement;
namespace {
void check(bool value,const char *why) { if(!value) throw std::runtime_error(why); }
bool exact(const Projection &a,const Projection &b) { return std::memcmp(a.data(),b.data(),sizeof(a))==0; }
void close(double a,double b,const char *why) { check(std::abs(a-b)<2e-6,why); }
std::array<double,4> clip(const Projection &m,const std::array<double,4> &point) {
    std::array<double,4> out{};
    for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column)
        out[row]+=double(m[4*column+row])*point[column];
    return out;
}
void displacement(const Projection &p,Position offset) {
    const auto before=p, shifted=offset_projection(p,offset);
    check(exact(before,p),"pure policy never changes its supplied input");
    for(size_t column=0;column<4;++column) for(size_t row:{size_t{2},size_t{3}})
        check(std::bit_cast<uint32_t>(p[4*column+row])==std::bit_cast<uint32_t>(shifted[4*column+row]),
              "every depth/W word remains exact, including off-diagonal words");
    for(const std::array<double,4> point:{std::array<double,4>{.1,-.2,-1,1},
            std::array<double,4>{-.7,.3,-4,1},std::array<double,4>{2,-1,-20,1}}) {
        const auto a=clip(p,point), b=clip(shifted,point);
        check(a[3]!=0 && a[3]==b[3] && a[2]==b[2],"depth and homogeneous W unchanged at different depths");
        close((b[0]/b[3]-a[0]/a[3])/2,offset.x,"positive X right, negative X left, full-width fraction");
        close((b[1]/b[3]-a[1]/a[3])/2,offset.y,"positive Y up, negative Y down, full-height fraction");
        for(const auto extent:nova::platform::standard_render_extents) {
            close(((b[0]/b[3]-a[0]/a[3])*extent.width/2)/extent.width,offset.x,
                  "all admitted render widths preserve normalized displacement");
            close(((b[1]/b[3]-a[1]/a[3])*extent.height/2)/extent.height,offset.y,
                  "all admitted render heights preserve normalized displacement");
        }
    }
}
}
int main() {
    try {
        static_assert(!kPositionAdapterConnected && !kPositionControlAvailable);
        static_assert(!nova::weapon_pose::kConsumedWeaponPoseBindingAvailable);
        RequestedPosition requested;
        check(requested.requested()==Position{},"default is original0, not an implied custom offset");
        check(requested.set({.025f,-.04f}),"independent requested pair accepted");
        const auto saved=requested.requested();
        for(Position bad: {Position{.05001f,0},Position{0,-.05001f},
                Position{std::numeric_limits<float>::infinity(),0},
                Position{0,std::numeric_limits<float>::quiet_NaN()}}) {
            check(!requested.set(bad) && requested.requested()==saved,"invalid pair never partially changes host request");
        }
        for(auto text:{"nan","inf","-inf","0.051","-0.051","1e99","0.01junk",""," 0","0 0"})
            check(!parse_coordinate(text),"malformed/nonfinite/out-of-range serialized coordinates reject");
        check(parse_coordinate("0.05")==.05f && parse_coordinate("-0.05")==-.05f,
              "finite inclusive bounds accepted");
        check(parse_coordinate("-0")==0.f && !std::signbit(*parse_coordinate("-0")),"serialized negative zero canonicalizes");
        Projection bits{};
        for(size_t i=0;i<bits.size();++i) bits[i]=std::bit_cast<float>(0x3f000000u+static_cast<uint32_t>(i));
        bits[2]=-0.f; bits[9]=std::bit_cast<float>(0x7fc12345u);
        check(exact(offset_projection(bits,{}),bits) && exact(offset_projection(bits,{-0.f,-0.f}),bits),
              "zero fast path copies all original bytes including signed zero and NaN payload");
        check(exact(offset_projection(bits,saved),bits),"invalid supplied projection returns original without poisoning output");
        constexpr double pi=3.14159265358979323846;
        //16:9 is current support;16:10/21:9 are robustness-only authored examples.
        for(double aspect:{16.0/9,16.0/10,21.0/9}) for(double horizontal:{60.0,80.116009,104.0}) {
            Projection p{}; p[0]=static_cast<float>(1/std::tan(horizontal*pi/360));
            p[5]=static_cast<float>(p[0]*aspect); p[10]=-1.002f; p[11]=-1; p[14]=-.2002f;
            for(Position offset:{Position{},Position{.05f,0},Position{-.05f,0},
                    Position{0,.05f},Position{0,-.05f},Position{.025f,-.04f}}) displacement(p,offset);
        }
        // Non-diagonal and noncanonical W row distinguish a homogeneous shift
        // from guessed writes to only projection[8]/[9] or eye-space translation.
        Projection general{2,.1f,.2f,.03f, .4f,3,.5f,-.02f, .6f,.7f,-1,-1, .8f,.9f,-.2f,.1f};
        displacement(general,saved);
        auto overflow=general; overflow[0]=overflow[3]=std::numeric_limits<float>::max();
        check(exact(offset_projection(overflow,{.05f,.04f}),overflow),"overflow rejects the entire transformed pair");
        using namespace nova::camera::viewmodel_eligibility;
        Boundary current{7,8,{1,2,3,4,5}}; // Authored values confer no native authority.
        for(Reason reason:{Reason::unproved,Reason::native_non_fps,Reason::owner_lost,Reason::lifetime_changed,Reason::ordinary_fps}) {
            const auto native=Decision::native(reason);
            check(exact(projection_for_supplied_frame(general,requested.requested(),native,current),general),
                  "missing/ineligible/unbound scope returns exact original projection");
            check(requested.requested()==saved,"native fallback never erases saved custom request");
            ++current.consumption;
        }
        requested.reset();
        check(requested.requested()==Position{} && !std::signbit(requested.requested().x) &&
              !std::signbit(requested.requested().y),"Reset restores canonical original0 pair");
        check(exact(offset_projection(general,requested.requested()),general),"Reset is byte-exact original matrix");
        std::puts("weapon position: authored pure math/settings only; native binding and visibility remain pending");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"weapon position: %s\n",e.what()); return 1; }
}
