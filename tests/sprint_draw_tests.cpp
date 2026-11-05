#include "input/native_draw.h"
#include "abi/guest_hook.h"
#include "bionic/runtime.h"
#include "fixtures/draw_contract.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace {
void check(bool ok,const char *message) { if (!ok) throw std::runtime_error(message); }
uint32_t original=0;
bool gameplay=false;
void __cdecl replacement(uint32_t sprite,uint32_t frame,uint32_t x,uint32_t y,
                         uint32_t flags,uint32_t sx,uint32_t sy,uint32_t alpha) {
    const uint32_t args[]{sprite,frame,x,y,flags,sx,sy,alpha};
    nova::input::draw_rush_overlay(gameplay,original,args);
}
void reset(SpriteDrawState &state,unsigned enabled=1,unsigned allowed=1,unsigned active=1,float stamina=.25f) {
    state={}; state.enabled=enabled; state.allowed=allowed; state.active=active; state.stamina=stamina;
    state.color=0xffffffff; state.clip[2]=1280; state.clip[3]=720;
}
void intact(const SpriteDrawState &state,unsigned enabled,unsigned allowed,unsigned active,float stamina) {
    check(state.enabled==enabled && state.allowed==allowed && state.active==active && state.stamina==stamina,
          "draw dispatch must not modify native sprint enabled/allowed/active/stamina values");
    check(state.percent_queries==enabled,"original percent calculation and eligibility remain authoritative");
    const std::array<uint32_t,4> full{0,0,1280,720};
    check(state.color==0xffffffff && !std::memcmp(state.clip,full.data(),sizeof(state.clip)),
          "native owner continues color/clip cleanup after the suppressed call");
}
void neighbors(const SpriteDrawState &state,unsigned first) {
    constexpr uint32_t expected[3][8]={{0x1111,32,1,2,4,5,6,7},
        {0x1111,57,8,9,10,11,12,13},{0x2222,57,14,15,16,17,18,19}};
    check(state.count==first+3,"only one exact site may be suppressed");
    for (unsigned i=0;i<3;++i) {
        check(!std::memcmp(state.arguments[first+i],expected[i],sizeof(expected[i])),
              "same-sprite cooldown, same-frame other site and another resource retain all eight arguments");
        check(state.colors[first+i]==0xffffffff && state.clips[first+i][1]==0 && state.clips[first+i][3]==720,
              "following HUD sees the original restored drawing state");
    }
}
void real_callsite(const char *path) {
    // Match the existing nonexecuting inspector's data-import setup. An empty
    // synthetic registry cannot map the real library's OpenSL/global objects.
    nova::ImportRegistry imports;
    nova::bionic::register_boot(imports);
    nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports);
    auto owner=nova::abi::require_function(image,"_ZN6CLevel17Render2DInterfaceEi");
    auto callee=nova::abi::require_function(image,"_ZNK7CSprite10PaintFrameEiffjffh");
    auto site=owner+0x1743;
    std::array<uint8_t,5> saved{};
    std::memcpy(saved.data(),reinterpret_cast<void *>(site),saved.size());
    {
        nova::abi::GuestCall hook(image,"_ZN6CLevel17Render2DInterfaceEi",0x1743,
            "_ZNK7CSprite10PaintFrameEiffjffh",reinterpret_cast<uint32_t>(&replacement));
        check(hook.original()==callee,"real exact call resolves expected sprite function");
    }
    check(!std::memcmp(saved.data(),reinterpret_cast<void *>(site),saved.size()),"real loaded bytes restored");
    std::puts("real rush call-site validated/restored; no initializers or guest functions executed");
}
}
int main(int argc,char **argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--real") {
            if (!std::filesystem::exists(argv[2])) { std::puts("real library missing; skipped"); return 77; }
            real_callsite(argv[2]); return 0;
        }
        check(argc==2,"synthetic fixture path required");
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        auto owner=nova::abi::require_function(image,"draw_owner");
        auto site_owner=nova::abi::require_function(image,"draw_rush_site");
        auto site=nova::abi::require_function(image,"draw_rush_call");
        auto offset=site-site_owner;
        original=nova::abi::require_function(image,"draw_paint");
        auto &state=*reinterpret_cast<SpriteDrawState *>(image.symbol("draw_state"));
        std::array<uint8_t,5> saved{};
        std::memcpy(saved.data(),reinterpret_cast<void *>(site),saved.size());

        // Before the fix, the owner's direct sprite call bypasses hidden button Draw.
        reset(state); nova::abi::invoke(owner,{57});
        check(state.count==4 && state.arguments[0][1]==57,"prior path actually draws targeted artwork");
        neighbors(state,1); intact(state,1,1,1,.25f);
        std::puts("baseline: targeted sprite=1; unrelated draws=3; native cleanup intact");

        // Reject mismatched instructions/targets without touching the loaded code.
        unsigned rejected=0;
        for (unsigned trial=0;trial<4;++trial) {
            try {
                nova::abi::GuestCall bad(image,"draw_rush_site",trial==0 ? 0 : trial==1 ? UINT32_MAX : offset,
                    trial==2 ? "hook_subject" : "draw_paint",trial==3 ? 0 : reinterpret_cast<uint32_t>(&replacement));
            } catch (const std::runtime_error &) { ++rejected; }
            check(!std::memcmp(saved.data(),reinterpret_cast<void *>(site),saved.size()),"rejection preserves original call bytes");
        }
        check(rejected==4,"non-CALL, overflow, wrong callee and null replacement rejected");

        {
            nova::abi::GuestCall hook(image,"draw_rush_site",offset,"draw_paint",reinterpret_cast<uint32_t>(&replacement));
            check(hook.original()==original,"exact site retains aligned forwarding target");
            unsigned cases=0;
            for (bool scope : {false,true}) for (unsigned enabled : {0u,1u})
            for (unsigned allowed : {0u,1u}) for (unsigned active : {0u,1u})
            for (float stamina : {0.f,.25f,1.f}) {
                gameplay=scope;
                reset(state,enabled,allowed,active,stamina);
                nova::abi::invoke(owner,{57});
                bool painted=enabled && stamina>0 && !scope;
                check(state.count==(painted ? 4u : 3u),"active on-foot dispatch must suppress the direct rush graphic only");
                neighbors(state,painted ? 1 : 0);
                intact(state,enabled,allowed,active,stamina);
                if (painted) {
                    const uint32_t args[]{0x1111,57,0x42580000,0x42c80000,3,0,0,0xff};
                    check(!std::memcmp(state.arguments[0],args,sizeof(args)),"out-of-scope forwarding retains all eight ABI words");
                    check(state.clips[0][1]==100-static_cast<unsigned>(stamina*100) &&
                          state.colors[0]==(active ? 0xffffdf65u : 0xffffffffu),
                          "outside gameplay native percentage clip and active tint still reach original painter");
                }
                ++cases;
            }
            gameplay=true; reset(state); nova::abi::invoke(owner,{32});
            check(state.count==4 && state.arguments[0][1]==32,"unexpected non-rush frame at guarded site forwards unchanged");
            neighbors(state,1); intact(state,1,1,1,.25f);
            std::printf("corrected: %u scope/native-state cases; frame32/resource/other-site forwarding; cleanup passed\n",cases);
        }
        check(!std::memcmp(saved.data(),reinterpret_cast<void *>(site),saved.size()),"removing exact-call hook restores five original bytes");
        reset(state); nova::abi::invoke(owner,{57});
        check(state.count==4,"unhooked native owner draws original sprint graphic again");
        std::puts("sprint draw regression passed (synthetic aligned guest; no gameplay)");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr,"sprint draw test: %s\n",error.what()); return 1;
    }
}
