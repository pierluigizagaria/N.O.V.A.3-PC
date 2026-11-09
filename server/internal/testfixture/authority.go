package testfixture

import "strings"

// NativeAuthority is entirely synthetic and covers NativeCatalogue's two
// weapon type IDs. Tests need neither original archives nor delivered data.
func NativeAuthority() []byte {
	return []byte(`{"schema_version":16,"content_version":"synthetic-v1","library_sha256":"a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e","world_sha256":"` + strings.Repeat("b", 64) + `",
	"consumables":{"cooldown_ms":30000,"xp_multiplier":2,"critical_chance":0.05,"critical_multiplier":2,"speed_boost":0.2,"precision_ms":60000,"agility_ms":60000,"perception_ms":60000,"camouflage_ms":60000,"countertech_ms":60000,"overcharge_ms":10000,"overcharge_health_cost":50},
	"player":{"base_health":125,"ffa_spawn_clearance_world_units":20,"body":{"radius_world_units":0.5,"height_world_units":2,"offset_x_world_units":0,"offset_y_world_units":0,"offset_z_world_units":0}},
	"weapon_addons":{"damage_percent":20,"fire_speed_percent":25,"launcher_delay_multiplier":0.5,"spread_reduction_percent":40,"reload_speed_percent":50,"ammo_capacity_percent":75},
	"collision":null,"mech_collision":null,"burst_fire":[],"pickups":[],"weapon_drops":[],"pickup_rules":{"first_dynamic_object_id":0,"dynamic_object_limit":0,"drop_lifetime_ms":0,"drop_ground_offset":0,"drop_ground_distance":0,"spawn_zones":[]},"scoring":{"first_kill":7,"streak_per_kill":9,"third_kill_same_victim":40,"win_bonus":25,"coins_percent":10},
	"weapons":[
	{"type_id":100,"wire_index":42,"reserve_ammo":80,"magazine_ammo":20,"spread_pattern":0,"spread_suppressed":false,"projectiles_per_shot":1,"base_damage_multiplier":1,"range_world_units":100,"minimum_fire_interval_ms":125,"base_reload":{"event_ms":400,"animation_ms":700,"animation_speed":1},"fire_modes":[{"projectile_type_id":2,"continuous":false,"continuous_delay_ms":200,"base_fire_delay_ms":150}]},
	{"type_id":101,"wire_index":43,"reserve_ammo":80,"magazine_ammo":20,"spread_pattern":0,"spread_suppressed":false,"projectiles_per_shot":1,"base_damage_multiplier":1,"range_world_units":100,"minimum_fire_interval_ms":125,"base_reload":{"event_ms":400,"animation_ms":700,"animation_speed":1},"fire_modes":[{"projectile_type_id":2,"continuous":false,"continuous_delay_ms":200,"base_fire_delay_ms":150}]}],
	"projectiles":[{"type_id":2,"damage":{"near_damage":30,"far_damage":10,"near_distance_world_units":5,"far_distance_world_units":20},"damage_per_second":false,"speed_world_units_per_second":800,"vehicle_damage_percent":50,"bullet":true,"ray":false,"railgun":false,"explosion_damage":0,"explosion_radius_world_units":0,"contact_radius_world_units":0,"ray_length_world_units":0,"active_time_ms":0,"lifetime_ms":0}],"melee":{"range_world_units":3,"damage":135,"cooldown_ms":2000,"target_range_world_units":3,"minimum_forward_dot":0,"sight_height_fraction":0.5},"mounts":[],"mounted_weapons":[],"vehicle_impact":{"minimum_health_fraction":0.1,"maximum_health_fraction":0.3,"damage_multiplier":4},"grenades":[],"equipment":[],"grenade_physics":{"gravity_world_units_per_second_squared":-9.8,"bounce_retention":0.5,"surface_clearance_world_units":0.05,"body_radius_world_units":0.15,"stop_speed_squared":0.01}}`)
}
