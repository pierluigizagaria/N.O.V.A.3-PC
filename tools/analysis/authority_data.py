"""Development readers for approved numeric authority definitions.

Offset-keyed intermediates are private parser evidence, never the distributed
schema. The final exporter must select semantically verified named fields and
discard every original string, dictionary, script and presentation resource.
"""
import math
import struct
from world_data import DictionaryStream


def float32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def animation_clip_start(start):
    """Original playback aligns the raw millisecond start to its30Hz key."""
    if type(start) is not int or not 0 <= start <= 0x3fffffff:
        raise ValueError('Invalid animation clip start')
    frame = math.floor(float32(float32(start * float32(.03)) + .5))
    return int(float32(frame * float32(1000 / 30)))


def actor_collision(reader):
    # Component13: radius, height, contact-damage, XYZ offset.
    return reader.floats(6)


def target_bodies(reader):
    """Private component141 bindings; export only derived numeric capsules."""
    tuple(flag(reader) for _ in range(3))
    reader.string(); reader.integer(); flag(reader); flag(reader)
    rows = tuple((reader.floats(10), flag(reader), reader.string())
                 for _ in range(reader.count(45)))
    reader.string(); reader.floats(1); reader.string(); flag(reader)
    for _ in range(4):
        reader.string()
    return rows


def ragdoll_settings(reader):
    """Component140 private definitions; bone bindings never leave development.

    CRagdoll::Init consumes radius/mass/SP/MP damage and axial extension from
    each body record. Bind-pose bone distance is a separate input, and current
    animation supplies the bone transform. These records are not static hitboxes.
    """
    skeletons = []
    for _ in range(reader.count(12)):
        kind = reader.integer()
        parts = []
        for _ in range(reader.count(45)):
            ids = tuple(reader.integer() for _ in range(3))
            bones = (reader.string(), reader.string())
            parameters = reader.floats(6)
            parts.append((ids, bones, parameters, flag(reader)))
        joints = []
        for _ in range(reader.count(28)):
            joints.append((tuple(reader.integer() for _ in range(4)), reader.floats(3)))
        skeletons.append((kind, tuple(parts), tuple(joints)))
    death_rules = tuple((reader.integer(), reader.integer()) for _ in range(reader.count(8)))
    return tuple(skeletons), death_rules


def physics_ragdoll(reader):
    """Component142's selected skeleton and private runtime parameters."""
    enabled, skeleton, dynamic = flag(reader), reader.integer(), flag(reader)
    parameters = reader.floats(17)
    binding = reader.string()
    return enabled, skeleton, dynamic, parameters, binding, flag(reader)


def remote_player(reader):
    """Component44 private motion parameters and animation bindings.

    Bone, clip, state and audio names are only development lookups; exporting
    this structure wholesale would violate the collision-data boundary.
    """
    parameters, integer = reader.floats(17), reader.integer()
    weapons = []
    for _ in range(reader.count(30)):
        fields = (reader.integer(), reader.integer(), reader.string(),
                  reader.integer(), reader.string(), flag(reader), flag(reader))
        animations = tuple(reader.string() for _ in range(reader.count(4)))
        states = tuple(reader.string() for _ in range(reader.count(4)))
        weapons.append((fields, animations, states))
    animations = tuple(reader.string() for _ in range(reader.count(4)))
    bindings = tuple(reader.string() for _ in range(6))
    return parameters, integer, tuple(weapons), animations, bindings


def actor_animation(reader):
    """Component12 private actor resources, graph and bone filter bindings."""
    resource, initial = reader.string(), reader.string()
    speed, enabled = reader.floats(1)[0], flag(reader)
    graph = reader.string()
    flags = (flag(reader), flag(reader))
    filters = tuple(tuple((reader.string(), reader.floats(1)[0])
                          for _ in range(reader.count(8)))
                    for _ in range(reader.count(4)))
    return resource, initial, speed, enabled, graph, flags, filters, tuple(flag(reader) for _ in range(3))


def player_body(local, remote):
    if local != remote or len(local) != 6 or not all(math.isfinite(v) for v in local):
        raise ValueError('Unverified player collision definitions')
    radius, height, _, x, y, z = local
    if radius <= 0 or height <= 0:
        raise ValueError('Invalid player collision dimensions')
    return {'radius_world_units': radius, 'height_world_units': max(height, float32(2*radius)),
            'offset_x_world_units': x, 'offset_y_world_units': y, 'offset_z_world_units': z}


def weapon_addons(local, remote):
    """Only the common fields consumed by AddOnsModifyWeapons are exported.

    Perks in the same component differ between local/remote templates and are
    deliberately excluded. The native consumer proves percent vs scalar units.
    """
    fields = {'damage_percent': 0x68, 'fire_speed_percent': 0x6c,
              'launcher_delay_multiplier': 0x70, 'spread_reduction_percent': 0x74,
              'reload_speed_percent': 0x78, 'ammo_capacity_percent': 0x7c}
    result = {}
    for name, offset in fields.items():
        value = local[offset]
        if value != remote[offset] or not math.isfinite(value) or value < 0:
            raise ValueError(f'Unverified weapon addon field {name}')
        result[name] = value
    if result['spread_reduction_percent'] > 100 or result['launcher_delay_multiplier'] <= 0:
        raise ValueError('Invalid weapon addon factors')
    return result


def firing_interval(states, clips):
    """Fastest authored mean discharge rate; no state scripts are exported.

    Single-shot attack length also bounds repeated trigger presses. This is
    an admission envelope, not a replacement animation state machine. Native
    cooldowns are applied separately; finite network bursts are a server policy.
    """
    intervals = []
    for _, kind, _, _, animations, _ in states:
        if kind not in (12, 37):
            continue
        for name, speed, tracks in animations:
            if speed == 0:  # Disabled unused loops exist in the shipped data.
                continue
            if not math.isfinite(speed) or speed < 0 or name not in clips:
                raise ValueError('Invalid attack animation timing')
            start, end = clips[name]
            duration = end - animation_clip_start(start)
            events = sorted({e[1] for _, _, rows in tracks for e in rows
                             if e[3] in (1, 2, 3) and 0 <= e[1] < duration})
            if not events or duration <= 0:
                continue
            # Events on both sides of a loop boundary may be only1ms apart;
            # that burst does not authorize a sustainable1000Hz firing rate.
            intervals.append(float32(float32(duration / len(events)) / speed))
    if not intervals or not all(math.isfinite(v) and v > 0 for v in intervals):
        raise ValueError('Missing authored firing interval')
    return min(intervals)


def reload_timing(states, clips):
    """Base reload timing only; all source names stay inside development tools.

    Event4 invokes DoReload. GetStateEventTime divides its start by the authored
    speed in float32, then truncates. The state controller uses GetAnimLength's
    rounded 30fps start, not the raw clip duration. Runtime perks are separate.
    """
    reloads = [s for s in states if s[0] == b'Reload' and s[1] == 22]
    if len(reloads) != 1 or reloads[0][2] or len(reloads[0][4]) != 1:
        raise ValueError('Unsupported reload state')
    name, speed, tracks = reloads[0][4][0]
    if not math.isfinite(speed) or speed <= 0 or name not in clips:
        raise ValueError('Invalid reload animation timing')
    events = [e for _, _, rows in tracks for e in rows if e[3] == 4]
    if len(events) != 1 or events[0][1] < 0 or events[0][2] < events[0][1]:
        raise ValueError('Ambiguous reload refill event')
    start, end = clips[name]
    duration = end - animation_clip_start(start)
    refill = int(float32(events[0][1] / speed))
    finish = math.ceil(float32(duration / speed))
    if not 0 <= refill <= finish or finish <= 0:
        raise ValueError('Reload event lies outside its duration')
    return {'event_ms': events[0][1], 'animation_ms': duration, 'animation_speed': speed}


def flag(reader):
    value = reader.unpack('B')[0]
    if value not in (0, 1):
        raise ValueError('Invalid authority flag')
    return bool(value)


def multifire(reader):
    fields = {0: flag(reader), 4: reader.integer(), 8: flag(reader)}
    for offset in range(0x0c, 0x2c, 4):
        fields[offset] = reader.integer()
    reader.string()
    reader.string()
    fields[0x34], fields[0x38] = reader.integer(), reader.integer()
    for _ in range(reader.count(4)):
        reader.string()
    for _ in range(3):
        reader.string()
    fields[0x54] = flag(reader)
    return fields


def weapon(reader):
    reader.string()
    reader.string()
    for _ in range(reader.count(4)):
        reader.string()
    fields = {0x18: reader.integer()}
    fields[0x1c] = tuple(multifire(reader) for _ in range(reader.count(65)))
    fields[0x28] = reader.integer()
    for _ in range(4):
        reader.string()
    for offset in range(0x3c, 0x3f):
        fields[offset] = flag(reader)
    for offset in range(0x40, 0x58, 4):
        fields[offset] = reader.integer()
    fields[0x58] = reader.floats(1)[0]
    fields[0x5c], fields[0x60] = reader.integer(), reader.integer()
    for offset in range(0x64, 0x90, 4):
        fields[offset] = reader.floats(1)[0]
    fields[0x90], fields[0x94] = flag(reader), reader.integer()
    reader.string()
    fields[0x9c] = flag(reader)
    for offset in range(0xa0, 0xb0, 4):
        fields[offset] = reader.integer()
    reader.string()
    reader.string()
    fields[0xb8], fields[0xb9] = flag(reader), flag(reader)
    fields[0xbc], fields[0xc0] = reader.floats(1)[0], flag(reader)
    for offset in range(0xc4, 0x100, 4):
        fields[offset] = reader.floats(1)[0]
    fields[0x100] = reader.integer()
    reader.string()
    fields[0x108] = reader.integer()
    reader.string()
    fields[0x110], fields[0x114] = reader.floats(2)
    fields[0x118], fields[0x119] = flag(reader), flag(reader)
    for offset in range(0x11c, 0x12c, 4):
        fields[offset] = reader.floats(1)[0]
    return fields


def weapon_template(data, index, kind):
    return numeric_template(data, index, kind, 71, weapon)


def health(reader):
    fields = {4: reader.floats(1)[0], 8: flag(reader), 0xc: reader.floats(1)[0],
              0x10: reader.integer(), 0x14: reader.integer()}
    # Sound/effect tables are consumed for exact boundary verification only.
    for stride in (8, 4, 8, 8, 4, 4):
        for _ in range(reader.count(stride)):
            reader.string()
            if stride == 8:
                reader.floats(1)
    for _ in range(6):
        reader.string()
    for _ in range(reader.count(4)):
        reader.string()
    fields[0x84] = tuple(reader.unpack('2i') for _ in range(reader.count(8)))
    for _ in range(reader.count(10)):
        reader.string(); reader.integer(); flag(reader); flag(reader)
    reader.string()
    return fields


def projectile(reader):
    fields = {4: flag(reader)}
    for offset in range(8, 0x34, 4):
        fields[offset] = reader.floats(1)[0]
    reader.string()
    reader.string()
    fields[0x3c], fields[0x40] = reader.floats(2)
    fields[0x44], fields[0x48] = flag(reader), reader.floats(1)[0]
    # Collision effects, sounds and decals are presentation only. Consume for
    # boundary verification; do not retain resource names or their parameters.
    for _ in range(reader.count(12)):
        reader.integer(); reader.string(); reader.integer()
    fields[0x58] = reader.integer()
    for _ in range(reader.count(8)):
        reader.integer()
        for _ in range(reader.count(8)):
            reader.string(); reader.floats(1)
    for _ in range(reader.count(12)):
        reader.integer(); reader.integer(); reader.floats(1)
    fields[0x74] = flag(reader)
    reader.string(); reader.string()
    fields[0x80] = reader.floats(1)[0]
    for offset in range(0x84, 0x88):
        fields[offset] = flag(reader)
    reader.string()
    fields[0x8c], fields[0x90] = reader.floats(1)[0], flag(reader)
    fields[0x94], fields[0x98] = reader.integer(), reader.floats(1)[0]
    fields[0x9c], fields[0xa0] = reader.integer(), reader.integer()
    fields[0xa4], fields[0xa5], fields[0xa8] = flag(reader), flag(reader), reader.integer()
    return fields


def damage(reader):
    fields = {4: reader.integer()}
    for offset in range(8, 0x2c, 4):
        fields[offset] = reader.floats(1)[0]
    fields[0x2c] = flag(reader)
    fields[0x30], fields[0x34] = reader.integer(), reader.integer()
    fields[0x38] = tuple((reader.integer(), reader.floats(1)[0]) for _ in range(reader.count(8)))
    fields[0x44], fields[0x48] = flag(reader), reader.integer()
    reader.string()
    return fields


def grenade(reader):
    fields = {4: reader.integer()}
    for offset in range(8, 0x1c, 4):
        fields[offset] = reader.floats(1)[0]
    fields[0x1c] = flag(reader)
    fields[0x20], fields[0x24] = reader.floats(2)
    fields[0x28] = reader.integer()
    reader.string()
    fields[0x30] = reader.integer()
    for _ in range(reader.count(4)):
        reader.integer()
    fields[0x40], fields[0x44] = reader.integer(), reader.integer()
    for _ in range(3):
        reader.string()
    for _ in range(reader.count(4)):
        reader.string()
    for _ in range(reader.count(12)):
        reader.integer(); reader.integer(); reader.floats(1)
    return fields


def state_animation(reader):
    # Names below are private references used to resolve numeric timing only.
    # Source scripts/sounds/effects are consumed and immediately discarded.
    name, speed = reader.string(), reader.floats(1)[0]
    game_tracks = []
    for _ in range(reader.count(12)):
        first, second = reader.string(), reader.string()
        events = []
        for _ in range(reader.count(20)):
            event = reader.string()
            events.append((event, *(reader.integer() for _ in range(4))))
        game_tracks.append((first, second, tuple(events)))
    for _ in range(reader.count(12)):
        reader.string(); reader.string()
        for _ in range(reader.count(37)):
            reader.string(); reader.integer(); reader.integer(); flag(reader); flag(reader)
            reader.integer(); flag(reader); flag(reader); flag(reader)
            reader.floats(1); reader.integer(); reader.string()
            for _ in range(reader.count(4)):
                reader.string()
    for _ in range(reader.count(12)):
        reader.string(); reader.string()
        for _ in range(reader.count(20)):
            reader.string(); reader.integer(); reader.integer(); reader.string(); reader.integer()
    for _ in range(reader.count(4)):
        for _ in range(reader.count(35)):
            reader.integer(); reader.integer(); reader.string(); reader.string()
            reader.floats(3); flag(reader); reader.floats(1); flag(reader); flag(reader)
    return name, speed, tuple(game_tracks)


def state_set(reader):
    slots = reader.integer()
    states = []
    for _ in range(reader.count(49)):
        name, kind, enabled = reader.string(), reader.integer(), flag(reader)
        integers = tuple(reader.integer() for _ in range(8))
        animations = tuple(state_animation(reader) for _ in range(reader.count(24)))
        triggers = []
        for _ in range(reader.count(25)):
            values = tuple(reader.integer() for _ in range(5))
            active, target = flag(reader), reader.string()
            triggers.append((values, active, target))
        states.append((name, kind, enabled, integers, animations, tuple(triggers)))
    tree_resource = reader.string()
    return slots, tuple(states), tree_resource


def remote_mech(reader):
    """Private component160 bindings; export only reduced collision references."""
    respawn = reader.integer()
    muzzles = tuple(tuple((reader.string(), flag(reader), reader.integer())
                          for _ in range(reader.count(9))) for _ in range(2))
    bindings = tuple(reader.string() for _ in range(10))
    return respawn, muzzles, bindings, reader.integer(), reader.integer()


def multiplayer_loadout(reader):
    """Private numeric layout; export only fields with verified rule semantics."""
    fields = {offset: reader.floats(1)[0] for offset in range(4, 0x40, 4)}
    fields[0x40], fields[0x44] = reader.integer(), reader.integer()
    fields[0x48] = reader.floats(1)[0]
    for offset in range(0x4c, 0x5c, 4):
        fields[offset] = reader.integer()
    for _ in range(reader.count(4)):
        reader.string()  # Presentation strings never enter authority data.
    fields[0x68], fields[0x6c] = reader.integer(), reader.integer()
    fields[0x70] = reader.floats(1)[0]
    for offset in range(0x74, 0x84, 4):
        fields[offset] = reader.integer()
    return fields


def numeric_template(data, index, kind, component, parser):
    start, end = index[kind, component]
    reader = DictionaryStream(data)
    reader.pos = start - 9
    result = parser(reader)
    if reader.pos + 9 != end:
        raise ValueError(f'Authority reader disagrees with native boundary: {kind}/{component}')
    return result
