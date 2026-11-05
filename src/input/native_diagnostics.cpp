#include "input/native_diagnostics.h"
#include "abi/guest_hook.h"

namespace nova::input {
namespace {
template<class T = uint32_t> T read(uint32_t p, uint32_t offset = 0) {
    return *reinterpret_cast<const T *>(p + offset);
}
using abi::invoke;
using abi::invoke_bool;
constexpr uint32_t move_control = 0x28, rotate_control = 0x2c;
constexpr uint32_t jump_control = 0x38, aim_control = 0x3c, rush_control = 0x40, powers_control = 0x44;
}
void sample_native_controls(const NativeSnapshot &s, const char *reason, FILE *output) {
    auto control = [&](uint32_t slot) { return s.scheme ? invoke(read(read(s.scheme),slot),{s.scheme}) : 0; };
    auto enabled = [](uint32_t button) { return button && read<uint8_t>(button,0x1d); };
    auto p = s.player, object = read(p,4), weapons = read(p,0x170);
    auto index = weapons ? read<int>(weapons,0x160) : -1;
    uint32_t component = 0;
    if (weapons && index >= 0) {
        auto start = read(weapons,0x148), end = read(weapons,0x14c);
        if (end >= start && static_cast<uint32_t>(index) < (end-start)/24)
            component = read(start,static_cast<uint32_t>(index)*24+0xc);
    }
    auto powers = read(p,0x174);
    auto power_index = powers ? read<int>(powers,0x4c) : -1;
    auto power_ammo = powers && power_index >= 0 ? static_cast<int>(invoke(s.power_ammo,{powers,static_cast<uint32_t>(power_index)})) : -1;
    std::fprintf(output,"native-controls-state frame=%llu move=%d rotate=%d jump=%d aim=%d powers=%d normal_power=%d power_ammo=%d tutorial=%d/%d/%d rush_enabled=%d rush_allowed=%d item_enabled=%d can_grenade=%d power_type=%d grenade_tutorial=%d\n",
        s.frame,enabled(control(move_control)),enabled(control(rotate_control)),enabled(control(jump_control)),enabled(control(aim_control)),enabled(control(powers_control)),power_index,power_ammo,read<uint8_t>(s.normal,0x3d),read<uint8_t>(s.normal,0x41),read<uint8_t>(s.normal,0x42),enabled(control(rush_control)),read<uint8_t>(p,0x32c),enabled(read(s.normal,0x70)),
        powers ? invoke_bool(s.can_use_power,{powers,0}) : 0,
        powers && power_index >= 0 ? static_cast<int>(invoke(s.power_type,{powers,static_cast<uint32_t>(power_index)})) : -1,read<uint8_t>(s.normal,0x3e));
    auto ammo = [&](uint32_t offset) { auto value = component ? read(component,offset) : 0;
        return value ? read<int>(value) : -1; };
    std::fprintf(output,"native-sample frame=%llu reason=%s scheme=%s pos=%.3f,%.3f,%.3f yaw=%.3f pitch=%.3f mode=%u rush_input=%u rushing=%u rush_heading=%.1f velocity=%.3f,%.3f,%.3f weapon=%d ammo=%d/%d aiming=%u\n",
        s.frame,reason,s.scheme ? s.image.symbolize(read(s.scheme)).c_str() : "none",
        read<float>(object,0x54),read<float>(object,0x58),read<float>(object,0x5c),
        read<float>(object,0x68),read<float>(object,0x60),read(p,0x130),read<uint8_t>(p,0x44),component ? read<uint8_t>(component,0x64) : 0,
        s.rush_heading,read<float>(p,0x260),read<float>(p,0x264),read<float>(p,0x268),
        index,ammo(0x44),ammo(0x38),component ? read<uint8_t>(component,0x61) : 0);
}
}
