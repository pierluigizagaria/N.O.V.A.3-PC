"""Compile approved numeric data; original inputs are development-only.

The exported schema is semantic and allowlisted. Never serialize an intermediate
offset dictionary or source strings. Runtime uses data/authority.json, without
this tool, the native boundary oracle, game archives or a client installation.
"""
import hashlib
import json
import math
import re
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile

from authority_data import actor_collision, firing_interval, grenade, health, multiplayer_loadout, numeric_template, player_body, projectile, reload_timing, state_set, weapon_addons, weapon_template
from animation_metadata import weapon_clips
from world_data import DictionaryStream, technical_resources, template_index, template_component, object_types, level
from build_collision_data import compile_collision
from mounted_collision import rigid_capsules, mech_collision, gunner_anchors, car_occupants, interaction_capsules
from pickup_metadata import pickup_metadata


def definitions(gol, index, weapon_ids, archives):
    weapons, mounted_weapons, projectiles, grenades, bursts = [], [], {}, {}, []
    # CLevel::PreInit enumerates component71 from the ordered template map;
    # RegisterMpWeaponType appends that order to GameMpManager's wire table.
    wire_ids = sorted(kind for kind, component in index if component == 71)
    if len(wire_ids) > 256:
        raise ValueError('Weapon wire table exceeds byte indices')
    for kind in sorted(weapon_ids | {7001, 23060, 23105}):
        w = weapon_template(gol, index, kind)
        reader = DictionaryStream(gol)
        reader.pos = index[kind, 12][0] - 9
        resource = reader.string()
        states = numeric_template(gol, index, kind, 14, state_set)[1]
        clips = weapon_clips(archives, resource)
        timing = reload_timing(states, clips) if kind in weapon_ids else {'event_ms':0,'animation_ms':0,'animation_speed':1}
        interval = firing_interval(states, clips)
        modes = []
        for mode_index,mode in enumerate(w[0x1c]):
            if mode[0x18] != -1 and mode[0x14] > 0:
                bursts.append({'weapon_type_id':kind,'mode':mode_index,
                               'shots':mode[0x18],'pause_ms':mode[0x14]})
            shot = mode[4]
            modes.append({'projectile_type_id': shot, 'continuous': mode[8],
                          'continuous_delay_ms': mode[0xc], 'base_fire_delay_ms': mode[0x10]})
            if (shot, 40) in index:
                p = numeric_template(gol, index, shot, 40, projectile)
                projectiles[shot] = {
                    'type_id': shot,
                    'damage': {'near_damage': p[0xc], 'far_damage': p[8],
                               'near_distance_world_units': p[0x14], 'far_distance_world_units': p[0x10]},
                    'damage_per_second': p[0x84] or p[0x90],
                    'speed_world_units_per_second': p[0x24],
                    'vehicle_damage_percent': p[0x48],
                    'bullet': p[0x86], 'ray': p[0x87], 'railgun': p[0x90],
                    'explosion_damage': p[0x1c],
                    'explosion_radius_world_units': p[0x20],
                    'contact_radius_world_units': p[0x2c],
                    'ray_length_world_units': p[0x28],
                    'active_time_ms': p[0x94], 'lifetime_ms': p[0xa8],
                }
            elif (shot, 38) in index:
                g = numeric_template(gol, index, shot, 38, grenade)
                grenades[shot] = {'type_id': shot, 'time_to_live_ms': g[4],
                                 'damage_radius_world_units': g[0x10], 'sticky': g[0x1c]}
            else:
                raise ValueError(f'Unsupported projectile definition for weapon {kind}')
        target = weapons if kind in weapon_ids else mounted_weapons
        target.append({'type_id': kind, 'wire_index': wire_ids.index(kind),
                        'reserve_ammo': w[0x4c], 'magazine_ammo': w[0x50],
                        'projectiles_per_shot': w[0x44], 'base_damage_multiplier': w[0x64],
                        'spread_pattern':w[0x48], 'spread_suppressed':w[0x40] != 0,
                        'range_world_units': w[0x58], 'minimum_fire_interval_ms': interval,
                        'base_reload': timing, 'fire_modes': modes})
    # PreInit(4) registers every component38 in ordered template-map order.
    # Keep only numeric gameplay fields, never the original resource structure.
    for wire, kind in enumerate(sorted(k for k, component in index if component == 38)):
        g = numeric_template(gol, index, kind, 38, grenade)
        grenades[kind] = {'type_id': kind, 'wire_index': wire,
                         'time_to_live_ms': g[4], 'damage': g[8],
                         'damage_radius_world_units': g[0x10], 'speed_world_units_per_second': g[0x14],
                         'drag': g[0x18], 'sticky': g[0x1c], 'contact_radius_world_units': g[0x24],
                         'effect': g[0x28], 'effect_duration_ms': g[0x40], 'effect_strength': g[0x44]}
    # CPowerManager selects template2571/component73 in multiplayer. The first
    # vector holds power identity, stock and the thrown object's numeric ID.
    reader = DictionaryStream(gol)
    reader.pos = index[2571, 73][0] - 9
    reader.integer(); reader.integer()
    equipment = []
    for _ in range(reader.integer()):
        kind = reader.integer(); reader.string()
        stock, thrown = reader.integer(), reader.integer()
        reader.string(); auxiliary_weapon = reader.integer(); reader.unpack('B'); reader.string()
        if thrown in grenades:
            equipment.append({'type_id': kind, 'grenade_type_id': thrown, 'stock': stock,
                              'weapon_type_id':auxiliary_weapon,
                              'weapon_wire_index':wire_ids.index(auxiliary_weapon) if auxiliary_weapon in wire_ids else 255})
    melee = None
    for _ in range(reader.integer()):
        kind = reader.integer(); reader.string()
        distance, amount, thrown = reader.integer(), reader.floats(1)[0], reader.integer()
        reader.string(); reader.integer(); reader.unpack('BB')
        cost, cooldown = reader.integer(), reader.integer()
        if kind == 7:
            melee = {'range_world_units': distance, 'damage': amount, 'cooldown_ms': cooldown}
    if melee is None or reader.pos + 9 != index[2571, 73][1]:
        raise ValueError('Unsupported multiplayer melee data')
    # CComponentPlayerComponent::Load ends with angle/range then four strings.
    # PlayerComponent::CheckMelee uses this targeting range, not power7's radius.
    reader.pos = index[1003,42][1]-9-24
    angle, distance = reader.floats(2)
    for _ in range(4): reader.string()
    if reader.pos+9 != index[1003,42][1]:
        raise ValueError('Unsupported melee targeting tail')
    with (Path(__file__).resolve().parents[2]/'local/input/lib/x86/libNOVA3_neon.so').open('rb') as stream:
        elf = ELFFile(stream)
        stream.seek(next(elf.address_offsets(0xe7f46c)))
        angle_scale = struct.unpack('<f',stream.read(4))[0]
        stream.seek(next(elf.address_offsets(0xe7f3c8)))
        sight_height = struct.unpack('<f',stream.read(4))[0]
    melee.update(target_range_world_units=distance,
                 minimum_forward_dot=math.cos(angle*angle_scale),
                 sight_height_fraction=sight_height)
    base_health = [numeric_template(gol, index, kind, 17, health)[4] for kind in (1002, 1003)]
    if base_health[0] <= 0 or base_health[0] != base_health[1]:
        raise ValueError('Local/remote player base health disagrees')
    addons = weapon_addons(*(numeric_template(gol, index, kind, 58, multiplayer_loadout) for kind in (1002, 1003)))
    body = player_body(*(numeric_template(gol, index, kind, 13, actor_collision) for kind in (1002, 1003)))
    for kind in (1002, 1003):
        reader = DictionaryStream(gol)
        reader.pos = index[kind, 0][0] - 9
        reader.integer()
        transform = reader.floats(9)
        if transform != (0., 0., 0., 0., 0., 0., 1., 1., 1.):
            raise ValueError('Unsupported base player transform')
    gains = [numeric_template(gol, index, kind, component, lambda r: r.unpack('35i'))
             for kind, component in index if component == 117]
    if len(gains) != 1 or any(v < 0 for v in gains[0]):
        raise ValueError('Ambiguous XP gain definition')
    scoring = {'first_kill': gains[0][0], 'streak_per_kill': gains[0][3],
               'third_kill_same_victim': gains[0][7],
               'win_bonus': gains[0][31], 'coins_percent': gains[0][34]}
    return {'player': {'base_health': base_health[0], 'body': body}, 'weapon_addons': addons, 'scoring': scoring,
            'weapons': weapons, 'mounted_weapons': mounted_weapons, 'projectiles': [projectiles[k] for k in sorted(projectiles)],
            'grenades': [grenades[k] for k in sorted(grenades)], 'equipment': equipment, 'melee': melee,
            'burst_fire':bursts}


def consumable_rules(gol,index,library):
    # Original component15 on catalogue resource245003. Strings describe shop
    # presentation only and are consumed for exact loader-boundary validation.
    r=DictionaryStream(gol); start,end=index[245003,15]; r.pos=start-9
    cooldown=r.integer(); xp,chance=r.floats(2); precision=r.integer()
    critical,speed=r.floats(2); agility,perception,camo,counter,overcharge=r.unpack('5i')
    if r.integer()!=9: raise ValueError('Unexpected consumable enum count')
    for expected in range(9):
        if r.integer()!=expected: raise ValueError('Unexpected consumable enum order')
        r.string();r.integer();r.string();r.integer();r.string();r.string();r.integer();r.string();r.integer()
    if r.pos+9!=end: raise ValueError('Consumable reader boundary mismatch')
    with library.open('rb') as stream:
        elf=ELFFile(stream);stream.seek(next(elf.address_offsets(0xe7db48)))
        health_cost=struct.unpack('<f',stream.read(4))[0]
    return {'cooldown_ms':cooldown*1000,'xp_multiplier':xp,'critical_chance':chance,
            'critical_multiplier':critical,'speed_boost':speed,'precision_ms':precision*1000,
            'agility_ms':agility*1000,'perception_ms':perception*1000,'camouflage_ms':camo*1000,
            'countertech_ms':counter*1000,'overcharge_ms':overcharge*1000,
            'overcharge_health_cost':health_cost}


def build(root):
    library = root / 'local/input/lib/x86/libNOVA3_neon.so'
    library_hash = hashlib.sha256(library.read_bytes()).hexdigest()
    if library_hash != 'a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e':
        raise ValueError('Unsupported original library')
    archives = sorted((root / 'local/game/obb').glob('*.obb'))
    gol = technical_resources(archives)['gol.bin']
    world_hash = hashlib.sha256(gol).hexdigest()
    if world_hash != '26527276476c4e18b63b3b12f33b20925a63b30a5359d4a2d19a1e3cda628ff3':
        raise ValueError('Unsupported world library')
    mapping = json.loads((root / 'local/server/bin/data/mapping.json').read_text(encoding='utf-8-sig'))
    if mapping['library'] != library_hash:
        raise ValueError('Incompatible catalogue mapping')
    # Catalogue IDs select the multiplayer weapon definitions; no ordinal or
    # display-name heuristic is used to discover a weapon's ballistic identity.
    weapon_ids = {row['native']['type_id'] for row in mapping['rows']
                  if row['native']['group'] in ('primary', 'secondary')}
    oracle = root / 'build/win32/tests/Release/native-world-reader-tests.exe'
    index = template_index(gol, oracle, library)
    resources = technical_resources(archives)
    # GetRandomSpawnPoint's FFA branch reads this float from the pinned ELF.
    # Keep the authored clearance in versioned data, not in runtime code.
    with library.open('rb') as stream:
        elf = ELFFile(stream)
        stream.seek(next(elf.address_offsets(0xe7db5c)))
        ffa_clearance = struct.unpack('<f', stream.read(4))[0]
    result = {'schema_version': 16, 'content_version': 'nova3-1.0.7-r19',
              'library_sha256': library_hash, 'world_sha256': world_hash,
              **definitions(gol, index, weapon_ids, archives),
              'collision':compile_collision(gol,index,archives,weapon_ids)}
    result['consumables'] = consumable_rules(gol,index,library)
    result['mech_collision'] = mech_collision(gol,index,archives)
    result['player']['ffa_spawn_clearance_world_units'] = ffa_clearance
    # Runtime object identities/placements, not source resources or scripts.
    result['pickups'], result['weapon_drops'],spawn_zones = pickup_metadata(gol,index,resources,weapon_ids,archives)
    # MP_SpawnPowerUp / MP_RemovePowerUps: native dynamic namespace, grounding
    # offset/ray length and lifetime. These are server rules, not settings.
    result['pickup_rules'] = {'first_dynamic_object_id':900000,'dynamic_object_limit':1000000,
                              'drop_lifetime_ms':30000,'drop_ground_offset':.5,
                              'drop_ground_distance':10000,'spawn_zones':spawn_zones}
    types = object_types(gol)
    mounts = []
    for map_id, (filename, _) in enumerate(template_component(gol,index,1040,93)):
        counters = {1:0, 2:0, 3:0}
        static_wire = 12
        for _, kind, instance in level(resources[filename.decode('ascii').casefold()+'.lvc'],types):
            components = dict(types[kind].components) if kind in types else {}
            category = 1 if 150 in components else 2 if 160 in components else 3 if 67 in components else 0
            # prepareObjectForMPGame registers targetable non-AI objects.
            # Cars create their two target proxies before registering the car.
            proxy_indices = [0, 0]
            if category == 1:
                proxy_indices = [static_wire, static_wire+1]
                static_wire += 2
            object_wire = static_wire if 141 in components and 75 not in components else 0
            if object_wire:
                static_wire += 1
            if not category or not isinstance(instance,dict) or 0 not in instance:
                continue
            base = instance[0]
            modes = instance.get(121, template_component(gol,index,kind,121) if (kind,121) in index else -1)
            health_value = numeric_template(gol,index,kind,17,health)[4] if (kind,17) in index else 0
            body = numeric_template(gol,index,kind,13,actor_collision)
            mounted_weapon = kind if category == 3 else 23105 if category == 2 else 0
            respawn_ms = 0
            maximum_speed_kmh = 0
            capsules = []
            gunner = None
            occupants = None
            if category == 1:
                reader = DictionaryStream(gol); reader.pos=index[kind,150][0]-9
                mounted_weapon = reader.integer()
                reader.integer(); reader.integer(); physics_values = reader.floats(20)
                maximum_speed_kmh = physics_values[3] # Load initializes car+f4 from data+1c.
                respawn_ms = reader.integer()
                capsules = rigid_capsules(gol,index,archives,kind)
                occupants = car_occupants(gol,index,archives,kind)
            elif category == 2:
                reader = DictionaryStream(gol); reader.pos=index[kind,160][0]-9
                respawn_ms = reader.integer()
            elif category == 3:
                gunner=gunner_anchors(gol,index,archives,kind,base.position,base.rotation)
            mounts.append({'map':map_id,'category':category,'wire_index':counters[category],
                           'object_id':base.identifier,'type_id':kind,'position':base.position,
                           'rotation':base.rotation,'enabled':bool(base.flags[0]),'modes':modes,
                           'health':health_value,'radius_world_units':body[0], 'height_world_units':body[1],
                           'weapon_type_id': mounted_weapon,
                           'static_wire_index':object_wire,'proxy_wire_indices':proxy_indices,
                           'respawn_ms':respawn_ms,'capsules':capsules,
                           'maximum_speed_kmh':maximum_speed_kmh})
            mounts[-1]['gunner']=gunner
            mounts[-1]['occupants']=occupants
            mounts[-1]['interaction'] = interaction_capsules(gol,index,archives,kind,category,result['mech_collision'])
            counters[category] += 1
    result['mounts'] = mounts
    # Constants in StartRealThrow/Update/ChangeDirection, in original ELF space.
    with library.open('rb') as stream:
        elf = ELFFile(stream)
        def scalar(address):
            stream.seek(next(elf.address_offsets(address)))
            return struct.unpack('<f', stream.read(4))[0]
        result['grenade_physics'] = {'gravity_world_units_per_second_squared': -9.8,
                                     'bounce_retention': scalar(0xe77424),
                                     'surface_clearance_world_units': scalar(0xe77b54),
                                     'body_radius_world_units': struct.unpack('<f', struct.pack('<I', 0x3e19999a))[0],
                                     'stop_speed_squared': scalar(0xe77448)}
        result['vehicle_impact'] = {'minimum_health_fraction':scalar(0xe81890),
                                    'maximum_health_fraction':scalar(0xe82804),
                                    'damage_multiplier':scalar(0xe7db04)}
        # RemoteMech MoveTo/SetRotation and its inherited action blender.
        result['mech_collision']['tuning'] = {
            'contact_damage':struct.unpack('<f',struct.pack('<I',0x447a0000))[0],
            'movement_threshold':scalar(0xe7f430),'turn_threshold':scalar(0xe7f434),
            'yaw_step':scalar(0xe77b50),'yaw_gain':[scalar(0xe77424),scalar(0xe7f470)],
            'strafe_angles':[scalar(0xe817ac),scalar(0xe81854)],
            'reverse_angle':scalar(0xe7db60),
            'turn_angles':[scalar(0xe77450),scalar(0xe7db60),scalar(0xe81730)],
            'transition_ms':300,'activation_transition_ms':1500,'turn_transition_ms':200}
    output = root / 'server/data/authority.json'
    encoded = json.dumps(result, indent=2, allow_nan=False)
    # One numeric vector/knot per line keeps the reduced model readable without
    # spending most of the data-size budget on indentation inside scalar arrays.
    encoded = re.sub(r'\[\s*([-+0-9.eE,\s]+)\]',
                     lambda match: '['+', '.join(v.strip() for v in match.group(1).split(','))+']',encoded)
    output.write_text(encoded + '\n', encoding='utf-8', newline='\n')
    print(f'{output.relative_to(root)}: {len(result["weapons"])} weapons, '
          f'{len(result["projectiles"])} projectiles, {len(result["grenades"])} grenades')


if __name__ == '__main__':
    build(Path(__file__).resolve().parents[2])
