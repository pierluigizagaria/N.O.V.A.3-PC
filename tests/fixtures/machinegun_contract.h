#pragma once
#include "look_contract.h"
/* Original synthetic objects. No native game code or data is copied here. */
struct MachineGunWeapon { unsigned shots[2], stops, messages, firing, allowed[2]; };
struct MachineGunManager { unsigned presses, releases, firing; };
struct MachineGunContract {
    struct ContractInput look;
    float yaw, pitch, yaw_limit, pitch_minimum, pitch_maximum;
    float actor_yaw, actor_pitch;
    unsigned held[2], pressed[2], released[2], events[33], resets, consumers;
    unsigned exiting;
    struct MachineGunWeapon weapon;
    struct MachineGunManager manager;
};
