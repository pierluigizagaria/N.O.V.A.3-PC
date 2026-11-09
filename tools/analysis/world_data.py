"""Development readers for the owner-approved technical map package.

Only collision geometry, placements and gameplay metadata may leave local/.
These readers never export a source dictionary (which can contain Lua), meshes,
textures or audio. Archive access reuses quality_profiles; no runtime server
depends on this module or on original game files.
"""
from dataclasses import dataclass
import math
import os
import struct
import subprocess
import zipfile

from quality_profiles import records, unpack


def technical_resources(archives):
    """Read development archives in mount order (main, then patch).

    Original archive lookup is case insensitive. Reject ambiguous spelling
    within a mount instead of choosing an arbitrary collision file. No source
    member is written to disk; this result is private parser input only.
    """
    result = {}
    for path in archives:
        with zipfile.ZipFile(path) as archive:
            if 'levels.gla' not in archive.namelist():
                continue
            source = archive.read('levels.gla')
        mounted = {}
        for name, method, payload, size, crc in records(source, b'levels.gla'):
            key = name.casefold()
            if key != 'gol.bin' and not key.endswith(('.col', '.lvc')):
                continue
            if key in mounted:
                raise ValueError('Ambiguous technical resource name')
            mounted[key] = unpack(method, payload, size, crc)
        result.update(mounted)
    return result


class Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.pos = 0

    @property
    def remaining(self):
        return len(self.data) - self.pos

    def take(self, size):
        if size < 0 or size > self.remaining:
            raise ValueError(f'Truncated technical data at {self.pos}')
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def unpack(self, fmt):
        return struct.unpack('>' + fmt, self.take(struct.calcsize('>' + fmt)))

    def integer(self):
        return self.unpack('i')[0]

    def count(self, minimum_record_size):
        value = self.integer()
        if minimum_record_size < 1 or not 0 <= value <= self.remaining // minimum_record_size:
            raise ValueError('Invalid technical record count')
        return value

    def floats(self, count):
        result = self.unpack(f'{count}f')
        if not all(math.isfinite(v) for v in result):
            raise ValueError('Non-finite technical coordinate')
        return result

    def finish(self):
        if self.remaining:
            raise ValueError(f'Unconsumed technical bytes: {self.remaining}')


class DictionaryStream(Reader):
    """CMemoryStream narrow DICT layout, independently bounds checked."""
    def __init__(self, data):
        header = Reader(data)
        if bytes(header.take(4)) != b'DICT':
            raise ValueError('Invalid technical stream signature')
        offset, wide = header.unpack('IB')
        if wide != 0 or not 9 <= offset <= len(data) - 4:
            raise ValueError('Unsupported technical dictionary layout')
        dictionary = Reader(memoryview(data)[offset:])
        strings = []
        for _ in range(dictionary.count(4)):
            length = dictionary.count(1)
            strings.append(bytes(dictionary.take(length)))
        dictionary.finish()
        super().__init__(memoryview(data)[9:offset])
        self.strings = strings

    def string(self):
        index = self.integer()
        # -1 is the original empty string representation, not Python indexing.
        if index == -1:
            return b''
        if not 0 <= index < len(self.strings):
            raise ValueError('Invalid technical dictionary reference')
        return self.strings[index]


@dataclass(frozen=True)
class Triangle:
    vertices: tuple
    material: int


@dataclass(frozen=True)
class CollisionGroup:
    # Internal group identity. The static collision path applies the owning
    # object's transform; this is not necessarily a BDAE render-node name.
    node: bytes
    triangles: tuple


def collision(data):
    """Decode CCollisionComponent::Load's groups, 9 floats + u8 material.

    Material numbers are preserved. Visibility does not imply passability.
    This returns local collision coordinates, never an implicitly placed world.
    """
    reader = DictionaryStream(data)
    groups = []
    for _ in range(reader.count(8)):
        node = reader.string()
        if not node or b'\0' in node:
            raise ValueError('Invalid collision node')
        # Multiple groups may refer to one scene node (the original console
        # asset includes repeated empty groups). Preserve their order.
        triangles = []
        for _ in range(reader.count(37)):
            vertices = reader.floats(9)
            material = reader.unpack('B')[0]
            triangles.append(Triangle(vertices, material))
        groups.append(CollisionGroup(node, tuple(triangles)))
    reader.finish()
    return tuple(groups)


@dataclass(frozen=True)
class ObjectType:
    components: tuple
    template_start: int
    template_end: int
    references: tuple = ()


def object_types(data):
    """Read GOL's indexed type headers without exporting template content."""
    reader = DictionaryStream(data)
    if reader.unpack('HH') != (0x474f, 3):
        raise ValueError('Unsupported object library')
    offsets = reader.unpack(f'{reader.count(4)}I')
    minimum = reader.pos + 9
    if any(not minimum <= p < len(reader.data) + 9 for p in offsets) or list(offsets) != sorted(set(offsets)):
        raise ValueError('Invalid object library index')
    result = {}
    for index, offset in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < len(offsets) else len(reader.data) + 9
        reader.pos = offset - 9
        kind = reader.integer()
        references = tuple(reader.integer() for _ in range(reader.count(4)))
        count = reader.unpack('h')[0]
        reader.string()  # Editor label, never part of the technical output.
        if not 0 <= count <= reader.remaining // 4:
            raise ValueError('Invalid component count')
        components = tuple(reader.unpack('hh') for _ in range(count))
        if kind in result or len({c for c, _ in components}) != count or any(c < 0 or g not in (0, 1) for c, g in components):
            raise ValueError('Invalid object component header')
        if reader.pos + 9 > end:
            raise ValueError('Overlapping object templates')
        result[kind] = ObjectType(components, reader.pos + 9, end, references)
    return result


def template_index(data, oracle, library):
    """Development-only original-reader oracle; returns numeric ranges.

    The oracle pins the library and GOL identities, executes template readers
    without a game scene, and reports unused indexed tails separately. No
    executable or original input is included in the technical package.
    """
    types = object_types(data)
    process = subprocess.run([str(oracle), str(library), '--index-templates'],
                             input=data, capture_output=True, timeout=30,
                             env=dict(os.environ, NOVA_REAL_TESTS='1'), check=True)
    result = {}
    gaps = {}
    sections = process.stdout.decode('ascii').split('WORLD_INDEX 1\r\n')
    if len(sections) != 2:
        raise ValueError('Missing original template index marker')
    for line in sections[1].splitlines():
        fields = line.split()
        if len(fields) == 5 and fields[0] == 'T':
            kind, component_id, start, end = map(int, fields[1:])
            if kind not in types or component_id not in dict(types[kind].components):
                raise ValueError('Unexpected original template reader')
            t = types[kind]
            if not t.template_start <= start <= end <= t.template_end or (kind, component_id) in result:
                raise ValueError('Invalid original template range')
            result[kind, component_id] = start, end
        elif len(fields) == 4 and fields[0] == 'G':
            kind, start, end = map(int, fields[1:])
            if kind not in types or not types[kind].template_start <= start < end == types[kind].template_end:
                raise ValueError('Invalid unused template range')
            gaps[kind] = start, end
        elif line:
            raise ValueError('Unexpected original-reader output')
    for kind, t in types.items():
        cursor = t.template_start
        for component_id, _ in t.components:
            start, end = result.get((kind, component_id), (-1, -1))
            if start != cursor:
                raise ValueError('Incomplete original template index')
            cursor = end
        if cursor != gaps.get(kind, (t.template_end, t.template_end))[0]:
            raise ValueError('Unaccounted original template bytes')
    return result


def template_component(data, index, kind, component_id):
    reader = DictionaryStream(data)
    start, end = index[kind, component_id]
    reader.pos = start - 9
    value = component(reader, component_id)
    if reader.pos + 9 != end:
        raise ValueError(f'Technical component disagrees with original reader: {kind}/{component_id}')
    return value


@dataclass(frozen=True)
class Placement:
    identifier: int
    position: tuple
    rotation: tuple
    scale: tuple
    flags: tuple


def placement(reader):
    identifier = reader.integer()
    position, rotation, scale = (reader.floats(3) for _ in range(3))
    flags = reader.unpack('BB')
    if identifier < 0 or any(v not in (0, 1) for v in flags):
        raise ValueError(f'Invalid object placement ending at {reader.pos + 9}: id={identifier}, flags={flags}')
    return Placement(identifier, position, rotation, scale, flags)


@dataclass(frozen=True)
class Spawn:
    placement: Placement
    flags: tuple
    car: int
    car_waypoint: int
    machine_gun: int
    bike: int
    mech: int
    team: int
    linked_spawn: int


def spawn(reader):
    base = placement(reader)
    flags = reader.unpack('BBB')
    if any(v not in (0, 1) for v in flags):
        raise ValueError('Invalid spawn flags')
    reader.string()  # Script is deliberately consumed, not retained or run.
    return Spawn(base, flags, *reader.unpack('7i'))


def level_properties(reader):
    """Consume presentation properties; no rendering/script fields escape."""
    reader.take(4 + 4 + 20)
    reader.take(16 * reader.count(16))
    reader.take(16)
    reader.take(4 * reader.count(4))
    reader.take(4 + 16)
    reader.string()
    reader.take(3)
    for _ in range(reader.count(4)):
        reader.string()
    reader.string()
    reader.take(8)
    reader.string()
    reader.string()
    reader.take(4)
    reader.string()
    reader.string()
    reader.take(8)


def zone(reader):
    base = placement(reader)
    modes = reader.integer()
    reader.take(5 + 12)
    reader.take(13 * reader.count(13))
    reader.string()
    size = reader.count(1)
    return base, modes, reader.pos + 9 + size


def component(reader, kind):
    """Read an instance component; unsupported schemas always stop extraction."""
    if kind == 0:
        return placement(reader)
    if kind == 1:
        name = reader.string()
        return name, reader.integer(), reader.unpack('11B')
    if kind == 12:  # Animation metadata: not included in the server package.
        reader.string()
        reader.string()
        reader.take(5)
        reader.string()
        reader.take(2)
        for _ in range(reader.count(4)):
            for _ in range(reader.count(8)):
                reader.string()
                reader.take(4)
        reader.take(3)
        return None
    if kind == 25:
        flags = reader.unpack('5B')
        name = reader.string()
        links = tuple((reader.string(), reader.integer()) for _ in range(reader.count(8)))
        return flags, name, links, reader.integer()
    if kind == 93:
        reader.integer()  # Original default selection; not a server setting.
        levels = []
        for _ in range(reader.count(27)):
            reader.string()  # Editor label.
            filename = reader.string()
            for _ in range(3):
                reader.string()  # Presentation resources and localization key.
            flags = reader.unpack('3B')
            modes = reader.integer()
            if any(v not in (0, 1) for v in flags) or not 0 <= modes <= 0xffffffff:
                raise ValueError('Invalid multiplayer level metadata')
            if flags[2]:
                levels.append((filename, modes))
        return tuple(levels)
    if kind == 22:
        reader.string()
        return reader.unpack('i3fB')
    if kind == 68:
        enabled = reader.unpack('B')[0]
        reader.string()
        flags = reader.unpack('BB')
        reader.string()
        return enabled, flags
    if kind in (102, 103):
        return reader.unpack('fi')
    if kind == 121:
        return reader.integer()
    if kind == 154:
        reader.take(18)
        return None
    if kind == 155:
        values = reader.unpack('fifi')
        label = reader.string()
        return values, label
    if kind == 145:
        return tuple((reader.integer(), reader.string(), reader.unpack('BB')) for _ in range(reader.count(10)))
    if kind == 7:
        values = reader.unpack('iB3fi5B')
        reader.string()
        return values, reader.unpack('fB')
    if kind == 6:
        values = reader.unpack('i9fiB')
        reader.string()
        return values, reader.unpack('3iBiB')
    if kind == 21:
        return reader.unpack('4if')
    if kind == 23:
        return reader.integer()
    if kind == 162:
        return tuple(reader.unpack(f'{reader.count(4)}i') for _ in range(2))
    if kind == 166:
        return reader.floats(3), tuple(reader.unpack('i4B') for _ in range(6))
    if kind == 114:
        return reader.unpack('iiB')
    if kind == 101:
        for _ in range(reader.count(4)):
            reader.string()
        reader.take(14)
        reader.string()
        reader.take(4)
        return None
    if kind == 146:
        for _ in range(reader.count(13)):
            reader.string()
            reader.string()
            reader.take(5)
        return None
    if kind == 167:
        return reader.floats(2), reader.unpack('i4B')
    if kind == 168:
        return reader.floats(2), reader.unpack('Bi4Bi4B3i8B')
    if kind == 138:
        reader.take(20)
        return None
    if kind == 174:  # Instance effect, consumed but never exported.
        reader.string()
        reader.take(4)
        reader.string()
        reader.take(2)
        return None
    raise ValueError(f'Unsupported instance component {kind} at {reader.pos + 9}')


def level(data, types):
    """Walk object boundaries, never search for guessed record signatures.

    Development-only intermediate result. Export requires resolved transforms
    and all technical component semantics, not just successful byte parsing.
    """
    reader = DictionaryStream(data)
    if reader.unpack('HH') != (0x4e56, 3):
        raise ValueError('Unsupported level format')
    count = reader.count(4)
    result = []
    boundaries = set()
    zone_ends = []
    for _ in range(count):
        offset = reader.pos + 9
        boundaries.add(offset)
        kind = reader.integer()
        if kind == 58:
            level_properties(reader)
        elif kind == 61:
            reader.string()
            reader.take(5)
        elif kind == 62:
            value = zone(reader)
            zone_ends.append(value[-1])
            result.append((offset, kind, value))
        elif kind == 108:
            result.append((offset, kind, spawn(reader)))
        elif kind == 3001:
            result.append((offset, kind, reader.unpack('3i')))
        elif kind == 3002:
            base = placement(reader)
            zones = reader.unpack(f'{reader.count(4)}i')
            modes = reader.integer()
            reader.string()
            size = reader.count(1)
            end = reader.pos + 9 + size
            zone_ends.append(end)
            result.append((offset, kind, (base, zones, modes, end)))
        elif kind == 3009:
            result.append((offset, kind, (placement(reader), component(reader, 1), reader.floats(1))))
        elif kind in types:
            values = {c: component(reader, c) for c, shared in sorted(types[kind].components) if not shared}
            result.append((offset, kind, values))
        else:
            raise ValueError(f'Unsupported level object {kind} at {offset}')
    boundaries.add(reader.pos + 9)
    if any(end not in boundaries for end in zone_ends):
        raise ValueError('Zone extent does not end at an object boundary')
    if reader.integer() != -1:
        raise ValueError('Missing level terminator')
    reader.finish()
    return result


def collision_bound_parts(base, groups):
    """Private numeric input for the native AABB oracle, not packaged meshes.

    Physics keeps every created collision object in its bound-union vector,
    including objects removed from broadphase for visibility. Each source COL
    group has separate regular/material18-or19 shapes. Vertex scale precedes
    rigid rotation; negative scale swaps that axis's extrema.
    """
    parts = []
    f32 = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]
    for group in groups:
        for invisible in (False, True):
            triangles = [t for t in group.triangles if (t.material in (18, 19)) == invisible]
            if not triangles:
                continue
            lo, hi = [], []
            for axis in range(3):
                minimum = min(min(t.vertices[axis::3]) for t in triangles)
                maximum = max(max(t.vertices[axis::3]) for t in triangles)
                a, b = f32(minimum*base.scale[axis]), f32(maximum*base.scale[axis])
                lo.append(min(a, b)); hi.append(max(a, b))
            parts.append(tuple(base.position) + tuple(base.rotation) + tuple(lo) + tuple(hi))
    return parts


def collision_bounds(parts, oracle, library):
    if not 1 <= len(parts) <= 4096:
        raise ValueError('Invalid collision bound part count')
    data = struct.pack('<I', len(parts))
    for part in parts:
        if len(part) != 12 or any(not math.isfinite(v) or abs(v) > 1e9 for v in part):
            raise ValueError('Invalid collision bound part')
        data += struct.pack('<12f', *part)
    process = subprocess.run([str(oracle), str(library), '--collision-bounds'], input=data,
                             capture_output=True, timeout=30,
                             env=dict(os.environ, NOVA_REAL_TESTS='1'), check=True)
    rows = [line for line in process.stdout.decode('ascii').splitlines()
            if line.startswith('COLLISION_BOUNDS 1 ')]
    if len(rows) != 1:
        raise ValueError('Missing native collision bounds')
    result = tuple(float(v) for v in rows[0].split()[2:])
    if len(result) != 6 or any(not math.isfinite(v) or abs(v) > 1e9 for v in result) or any(result[i] >= result[i+3] for i in range(3)):
        raise ValueError('Invalid native collision bounds')
    return result


def collision_map(resources, index, filename):
    """Compile collision/spawn metadata in memory, without source dictionaries.

    This is one section of the technical map package, not an operator import
    workflow. Interactive-object rules are compiled separately. No file output
    is performed here, and missing geometry is never replaced by render data.
    """
    library = resources['gol.bin']
    types = object_types(library)
    objects = level(resources[filename.decode('ascii').casefold() + '.lvc'], types)
    colliders, spawns, zones, bounds_parts = [], [], [], []
    identifiers = set()
    # These two references are absent in the supported original level archive.
    # CCollisionComponent::Load returns without geometry when open fails
    # (0x5a03d4 -> 0x5a09fa). Other missing references require investigation.
    absent = {
        (b'DM_MAP_2_San_Francisco1', 235102, 'dm_map_2_sanfrancisco_decor.col'),
        (b'DM_MAP_2_San_Francisco1', 20150, 'dm_map_2_sanfrancisco_jet_pack_cap.col'),
    }

    def pose(base):
        return {'position': base.position, 'rotation': base.rotation, 'scale': base.scale}

    for _, kind, value in objects:
        if kind == 62:
            identifier = value[0].identifier
            if identifier < 0 or identifier in zones:
                raise ValueError('Invalid or duplicate zone identity')
            zones.append(identifier)
            continue
        if kind == 108:
            base = value.placement
            if base.identifier in identifiers:
                raise ValueError('Duplicate technical object ID')
            identifiers.add(base.identifier)
            spawns.append({'id': base.identifier, **pose(base),
                           'enabled': bool(value.flags[2]),
                           'level_start': bool(value.flags[0]), 'team': value.team,
                           'car': value.car, 'car_waypoint': value.car_waypoint,
                           'machine_gun': value.machine_gun, 'bike': value.bike,
                           'mech': value.mech, 'linked_spawn': value.linked_spawn})
            continue
        if not isinstance(value, dict) or 25 not in dict(types[kind].components):
            continue
        base = value.get(0)
        if base is None or base.identifier in identifiers:
            raise ValueError('Missing or duplicate collision placement')
        identifiers.add(base.identifier)
        flags, resource, links, _ = value[25] if 25 in value else template_component(library, index, kind, 25)
        if any(v not in (0, 1) for v in flags) or links:
            raise ValueError('Unsupported collision flags or linked-node transform')
        groups = ()
        if resource:
            key = resource.decode('ascii').casefold()
            if not key.endswith('.bdae') or '/' in key or '\\' in key:
                raise ValueError('Unsupported collision resource reference')
            key = key[:-5] + '.col'
            if key in resources:
                groups = collision(resources[key])
            elif (filename, base.identifier, key) not in absent:
                raise ValueError(f'Missing collision resource {key}')
        # Node names are unnecessary for unlinked object-space meshes and never
        # leave the reader. Preserve material IDs and local coordinates exactly.
        faces = tuple((triangle.vertices, triangle.material)
                      for group in groups for triangle in group.triangles)
        bounds_parts.extend(collision_bound_parts(base, groups))
        colliders.append({'id': base.identifier, 'type': kind, **pose(base),
                          'enabled': bool(base.flags[0]), 'static': bool(flags[1]),
                          'transparent': bool(flags[4]), 'triangles': faces})
    return {'colliders': tuple(colliders), 'spawns': tuple(spawns), 'zones': tuple(zones), 'bounds_parts': tuple(bounds_parts)}


def encode_collision_map(value, map_id, mode_bits):
    """Write the versioned, string-free collision section for the Go loader."""
    if not 0 <= map_id <= 255 or not 1 <= mode_bits <= 255:
        raise ValueError('Invalid technical map identity')
    colliders, spawns = value['colliders'], value['spawns']
    if len(colliders) > 4096 or not 1 <= len(spawns) <= 512:
        raise ValueError('Invalid technical map object count')
    out = bytearray(b'NV3C\x03')
    out += struct.pack('>BHII', map_id, mode_bits, len(colliders), len(spawns))

    def transform(item):
        coordinates = tuple(v for field in ('position', 'rotation', 'scale') for v in item[field])
        if len(coordinates) != 9 or any(not math.isfinite(v) or abs(v) > 1e9 for v in coordinates):
            raise ValueError('Invalid technical map transform')
        return struct.pack('>9f', *coordinates)

    remaining = 500000
    for item in colliders:
        faces = item['triangles']
        remaining -= len(faces)
        if remaining < 0:
            raise ValueError('Technical map triangle limit exceeded')
        flags = int(item['enabled']) | (int(item['static']) << 1) | (int(item['transparent']) << 2)
        out += struct.pack('>IIB', item['id'], item['type'], flags)
        out += transform(item)
        out += struct.pack('>I', len(faces))
        for vertices, material in faces:
            if len(vertices) != 9 or any(not math.isfinite(v) or abs(v) > 1e9 for v in vertices):
                raise ValueError('Invalid technical map triangle')
            out += struct.pack('>9fB', *vertices, material)
    for item in spawns:
        out += struct.pack('>I', item['id']) + transform(item)
        out += struct.pack('>B7i', int(item['enabled']) | (int(item['level_start']) << 1),
                           item['team'], item['car'], item['car_waypoint'], item['machine_gun'],
                           item['bike'], item['mech'], item['linked_spawn'])
    zones = value['zones']
    if len(zones) > 4096 or len(set(zones)) != len(zones) or any(not 0 <= v <= 0x7fffffff for v in zones):
        raise ValueError('Invalid technical zone identities')
    out += struct.pack('>I', len(zones))
    out += struct.pack(f'>{len(zones)}I', *zones)
    bounds = value['position_bounds']
    if len(bounds) != 6 or any(not math.isfinite(v) or abs(v) > 1e9 for v in bounds) or any(bounds[i] >= bounds[i+3] for i in range(3)):
        raise ValueError('Invalid map position bounds')
    out += struct.pack('>6f', *bounds)
    return bytes(out)
