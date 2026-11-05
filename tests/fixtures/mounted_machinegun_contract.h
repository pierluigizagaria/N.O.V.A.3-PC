#pragma once
#include "machinegun_contract.h"
struct MountedQueryEntry { unsigned object,type,component; };
struct MountedQueries { unsigned calls,count; struct MountedQueryEntry entries[16]; };
struct MountedMachineGun {
    struct MachineGunContract gun;
    float translation[3],parent_yaw,bank,tilt;
    float node_yaw,node_pitch,world_direction[3],camera_direction[3],world_position[3];
    float cached_tilt,camera_tilt_delta;
    unsigned ai_phases,bank_phases,player_phases,updates,dt;
    int duration,delay;
    float look_yaw,look_pitch;
};
