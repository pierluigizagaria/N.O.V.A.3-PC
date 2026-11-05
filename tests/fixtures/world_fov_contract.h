#pragma once
typedef struct WorldFovContract {
    unsigned char level[0x700], player[0x500], camera[0x420];
    unsigned char nodes[3][0x350], component[0x240], config[0x100], manager[0x200];
    unsigned queries, projections, matrices, setters, custom_sets, fast_nodes;
    unsigned ctor_words[4], dtor_words[2], component_words[2];
    unsigned nested_weapon, weapon_depth, replace_weapon, next_node, original_calls;
    unsigned fast_mutate, reset_calls;
    float weapon_fov[3], weapon_x[3], weapon_frustum[3], inside_x, inside_frustum;
    unsigned weapon_mode[3], weapon_limit, camera_setups, render_words[2], driver_node;
    float driver_projection[16], driver_view[16];
    float weapon_gpu_x[3], weapon_gpu_view[3], weapon_box[3], weapon_marker[3];
    float after_gpu_x[3], after_cpu_x[3], after_frustum[3], after_view[3];
    unsigned char weapon_manager[0x200], inventory[48], weapon_component[0x340], weapon_config[0x140], tweak[0x70];
    unsigned diff_calls,diff_manager,noop_set;
    float diff_sample,forwarded_sample;
    float first_cpu_x[3],first_frustum[3],first_raw[3];
    unsigned char eligibility_actor[0x140],eligibility_health[0x20],eligibility_mp[0x3c04];
    unsigned eligibility_mp_rows[13],eligibility_mp_player[1];
    unsigned char eligibility_app[0x18780],eligibility_state[0x40];
    unsigned eligibility_dead_calls;
    unsigned eligibility_poison_node,eligibility_poison_reads;
    unsigned eligibility_driver_calls,eligibility_view_calls;
} WorldFovContract;
