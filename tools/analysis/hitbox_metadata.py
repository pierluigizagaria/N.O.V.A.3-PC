"""Private animation inputs for deriving technical animated collision geometry.

Reads original resources in memory, never writes original tracks, bone bindings,
meshes or animation files. This module is development-only, not a server runtime
dependency. Sampling is verified against original native animation consumers.
State selection/blending and the final collision-only export are separate work.
"""
from bisect import bisect_right
from dataclasses import dataclass
import math
import struct
import zipfile

from animation_metadata import pak_index
from authority_data import float32 as f32
from quality_profiles import records, unpack


def actor_resource(archives, name, streamed, pack=None):
    if pack is not None and pack != 'weapons_stream.gla':
        raise ValueError('Unsupported private collision resource pack')
    archive_name = pack or ('actors_stream.gla' if streamed else 'actors.gla')
    result = None
    for path in archives:
        with zipfile.ZipFile(path) as archive:
            if archive_name not in archive.namelist():
                continue
            if streamed:
                with archive.open(archive_name) as stream:
                    index = pak_index(stream, archive.getinfo(archive_name).file_size)
                    entry = index.get(name.lower())
                    if entry is None:
                        continue
                    offset, size, flags = entry
                    if flags or size > 16 << 20:
                        raise ValueError('Unsupported collision animation resource')
                    stream.seek(offset)
                    result = stream.read(size)
            else:
                found = []
                for key, method, payload, size, crc in records(archive.read(archive_name), archive_name.encode()):
                    if key.casefold().encode() == name.lower():
                        found.append(unpack(method, payload, size, crc))
                if len(found) > 1:
                    raise ValueError('Ambiguous collision skeleton resource')
                if found:
                    result = found[0]
    if result is None:
        raise ValueError('Missing collision animation input')
    return result


class Resource:
    def __init__(self, data):
        self.data = bytes(data)
        if len(data) > 16 << 20 or self.take(0, 8) != b'BRES\xfe\xff\0\0' or self.word(8) != 60 or self.word(12) != len(data):
            raise ValueError('Unsupported collision animation resource')
        count = self.word(16)
        self.relocations = set(self.values(self.word(24), count, 'I'))
        if len(self.relocations) != count:
            raise ValueError('Duplicate animation relocation')
        self.root = self.pointer(32, 224)

    def take(self, position, size):
        if position < 0 or size < 0 or position+size > len(self.data):
            raise ValueError('Collision animation input exceeds resource')
        return self.data[position:position+size]

    def values(self, position, count, kind):
        if count < 0 or count > len(self.data)//struct.calcsize(kind):
            raise ValueError('Invalid collision animation count')
        return struct.unpack('<'+str(count)+kind, self.take(position, count*struct.calcsize(kind)))

    def word(self, position):
        return self.values(position, 1, 'I')[0]

    def pointer(self, position, size):
        if position not in self.relocations:
            raise ValueError('Missing collision animation relocation')
        target = self.word(position)
        self.take(target, size)
        return target

    def string(self, field):
        position = self.pointer(field, 1)
        length = self.word(position-4)
        if not 0 < length <= 256:
            raise ValueError('Invalid private bone binding')
        return self.take(position, length).rstrip(b'\0')


def slerp(a, b, fraction):
    """Original shortest-arc slerp, including normalized linear branch at .95."""
    dot = f32(f32(f32(f32(a[0]*b[0])+f32(a[1]*b[1]))+f32(a[2]*b[2]))+f32(a[3]*b[3]))
    if dot < 0:
        a, dot = tuple(-v for v in a), -dot
    if dot > f32(.95):
        value = tuple(f32(x+f32(f32(y-x)*fraction)) for x, y in zip(a, b))
        norm = f32(math.sqrt(f32(sum(f32(v*v) for v in value))))
        return tuple(f32(v*f32(1/norm)) for v in value) if norm else value
    angle = f32(math.acos(max(-1., min(1., dot))))
    inverse = f32(1/f32(math.sin(angle)))
    first = f32(f32(math.sin(f32(f32(1-fraction)*angle)))*inverse)
    second = f32(f32(math.sin(f32(fraction*angle)))*inverse)
    return tuple(f32(f32(x*first)+f32(y*second)) for x, y in zip(a, b))


def blend_values(kind, values, weights):
    """Original active-track blend: ordered quaternion SLERP or weighted XYZ.

    The graph supplies active children and their weights. One child passes
    through unchanged. This is deliberately not a global capsule interpolation.
    """
    dimensions = {1: 3, 5: 4}.get(kind)
    if not dimensions or not values or len(values) != len(weights):
        raise ValueError('Invalid collision blend')
    if any(len(row) != dimensions or not all(math.isfinite(v) for v in row) for row in values):
        raise ValueError('Invalid collision blend value')
    if any(not math.isfinite(w) or w <= 0 for w in weights):
        raise ValueError('Inactive or invalid collision blend weight')
    if len(values) == 1:
        return tuple(values[0])
    if kind == 1:
        result = [0.] * 3
        for row, weight in zip(values, weights):
            result = [f32(a+f32(v*weight)) for a, v in zip(result, row)]
        return tuple(result)
    result, total = values[0], weights[0]
    for row, weight in zip(values[1:], weights[1:]):
        total = f32(total+weight)
        result = slerp(result, row, f32(weight/total))
    return tuple(result)


def track_mask(targets, selected, selector):
    """Private exact-node mixer mask; matching does not propagate to children."""
    if selector not in (0,1):
        raise ValueError('Invalid collision track selector')
    return tuple(float((target in selected) == bool(selector)) for target in targets)


def mixer_weights(mask, weights, additive=False):
    if (len(weights) != 2 or any(not math.isfinite(w) or not 0 <= w <= 1 for w in weights)
            or not math.isfinite(mask) or not 0 <= mask <= 1):
        raise ValueError('Invalid collision mixer weight')
    second = f32(f32(mask)*f32(weights[1]))
    return (f32(weights[0]) if additive else f32(1-second),second)


def quaternion_product(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return (f32(f32(f32(f32(w*X)+f32(x*W))+f32(y*Z))-f32(z*Y)),
            f32(f32(f32(f32(w*Y)+f32(y*W))+f32(z*X))-f32(x*Z)),
            f32(f32(f32(f32(w*Z)+f32(z*W))+f32(x*Y))-f32(y*X)),
            f32(f32(f32(f32(w*W)-f32(x*X))-f32(y*Y))-f32(z*Z)))


def additive_values(kind, values, weights):
    """Native ordered quaternion powers, or signed position accumulation."""
    dimensions = {1:3,5:4}.get(kind)
    if (not dimensions or len(values) != len(weights)
            or any(len(row) != dimensions or not all(math.isfinite(v) for v in row) for row in values)
            or any(not math.isfinite(w) for w in weights)):
        raise ValueError('Invalid additive collision blend')
    if kind == 1:
        if len(values) == 1:
            return tuple(values[0])
        result = (0.,0.,0.)
        for row,weight in zip(values,weights):
            result = tuple(f32(a+f32(v*weight)) for a,v in zip(result,row))
        return result
    result = (0.,0.,0.,1.)
    for rotation,weight in zip(values,weights):
        rotation = tuple(map(f32,rotation))
        weight = f32(weight)
        if weight != 1:
            length = f32(math.sqrt(f32(f32(f32(rotation[0]**2)+f32(rotation[1]**2))+f32(rotation[2]**2))))
            if length <= f32(.000001) or not -1 <= rotation[3] <= 1:
                rotation = (0.,0.,0.,1.)
            else:
                angle = f32(f32(math.acos(rotation[3]))*weight)
                sine, cosine, inverse = f32(math.sin(angle)), f32(math.cos(angle)), f32(1/length)
                rotation = tuple(f32(f32(v*inverse)*sine) for v in rotation[:3])+(cosine,)
        result = quaternion_product(result,rotation)
    return result


def directional_weights(vector, angles):
    """Private directional blend in the actor's forward-Y/right-X frame.

    Angles and speed thresholds come from the private animation controller;
    neither these helpers nor original controller resources are server inputs.
    """
    if (len(vector) != 3 or not all(math.isfinite(v) and abs(v) < 1e10 for v in vector)
            or len(angles) < 2 or angles[0] != 0
            or any(not math.isfinite(a) or not 0 <= a < 360 for a in angles)
            or any(a >= b for a, b in zip(angles, angles[1:]))):
        raise ValueError('Invalid collision directional blend')
    x, y, z = map(f32, vector)
    length = f32(math.sqrt(f32(f32(f32(x*x)+f32(y*y))+f32(z*z))))
    if length:
        inverse = f32(1/length)
        x, y = f32(x*inverse), f32(y*inverse)
        angle = f32(f32(math.acos(max(-1., min(1., y))))*f32(180/math.pi))
        if x > 0:
            angle = f32(360-angle)
    else:
        angle = 0.
    first = max(0, bisect_right(angles, angle)-1)
    second = (first+1) % len(angles)
    end = angles[second] if second else 360.
    weight = f32(f32(angle-angles[first])/f32(end-angles[first]))
    return first, second, (f32(1-weight), weight)


def locomotion_weights(previous, current, thresholds, lengths, times):
    """Two-child walk/run weights, rates and preserved normalized phase.

    The previous nearest child supplies phase, even when the new speed selects
    the other child. Durations synchronize both clips during their crossfade.
    """
    if (len(thresholds) != 2 or len(lengths) != 2 or len(times) != 2
            or not all(math.isfinite(v) for v in (previous, current, *thresholds, *lengths, *times))
            or min(previous, current) < 0 or not 0 < thresholds[0] < thresholds[1]
            or min(lengths) <= 0 or min(times) < 0):
        raise ValueError('Invalid collision locomotion blend')
    previous, current = f32(previous), f32(current)
    low, high = map(f32, thresholds)
    old = 0 if previous <= f32(f32(low+high)*.5) else 1
    phase = f32(f32(times[old])/f32(lengths[old]))
    if current <= low:
        return (1., 0.), ((0, f32(current/low), phase),)
    if current > high:
        return (0., 1.), ((1, f32(current/high), phase),)
    fraction = f32(f32(current-low)/f32(high-low))
    complement = f32(1-fraction)
    ratio = f32(f32(lengths[0])/f32(lengths[1]))
    walk_rate = f32(1+f32(f32(ratio-1)*fraction))
    run_rate = f32(f32(complement/ratio)+fraction)
    return (complement, fraction), ((0, walk_rate, phase), (1, run_rate, phase))


@dataclass(frozen=True)
class TransitionBlend:
    """Private two-child controller transition, using native integer time."""
    default_duration: int
    duration: int
    elapsed: int
    direction: int
    weights: tuple

    def __post_init__(self):
        if (any(type(v) is not int or not 0 <= v <= 0x3fffffff
                for v in (self.default_duration, self.duration, self.elapsed))
                or self.elapsed > self.duration or self.direction not in (-1,0,1)
                or len(self.weights) != 2
                or any(not math.isfinite(w) or not 0 <= w <= 1 for w in self.weights)
                or abs(sum(self.weights)-1) > 1e-6):
            raise ValueError('Invalid collision transition')

    def advance(self, direction, duration_override, delta_ms):
        if (direction not in (-1,0,1) or type(duration_override) is not int
                or not -1 <= duration_override <= 0x3fffffff
                or type(delta_ms) is not int or not 0 <= delta_ms <= 0x3fffffff):
            raise ValueError('Invalid collision transition update')
        duration, elapsed = self.duration, self.elapsed
        if duration_override >= 0:
            duration = duration_override
            elapsed = int(f32(f32(duration)*f32(1-self.weights[0])))
        weights = self.weights
        if direction:
            elapsed += delta_ms if direction > 0 else -delta_ms
            if direction > 0 and elapsed >= duration:
                direction, duration, elapsed = 0, self.default_duration, self.default_duration
            elif direction < 0 and elapsed <= 0:
                direction, duration, elapsed = 0, self.default_duration, 0
            fraction = f32(f32(elapsed)/f32(duration)) if duration > 0 else 0.
            weights = (f32(1-fraction), fraction)
        return TransitionBlend(self.default_duration, duration, elapsed, direction, weights)


def aim_rotations(rotations, pitch_degrees):
    """Private on-foot root/spine/spine1/neck/clavicle pose adjustment.

    Pitch is distributed40/20/20/20 percent in actor coordinates, conjugated
    into each current parent frame. The two clavicles share the neck parent.
    Apply once to the newly sampled pose, never cumulatively across updates.
    """
    if len(rotations) != 6 or not math.isfinite(pitch_degrees) or not -360 <= pitch_degrees <= 360:
        raise ValueError('Invalid collision aim pose')
    if any(len(q) != 4 or not all(math.isfinite(v) for v in q) or abs(sum(v*v for v in q)-1) > .001 for q in rotations):
        raise ValueError('Invalid collision aim rotation')
    pitch = f32(pitch_degrees)
    if pitch > 180:
        pitch = f32(pitch-360)
    if pitch < -180:
        pitch = f32(pitch+360)
    result = list(rotations)
    for index in range(1, 6):
        parent = result[0]
        for ancestor in range(1, min(index, 4)):
            parent = quaternion_product(parent, result[ancestor])
        # Verified original degree-to-radian coefficients for40% and20%.
        angle = f32(pitch*(.006981316953897476 if index == 1 else .003490658476948738))
        offset = (f32(math.sin(angle*.5)), 0., 0., f32(math.cos(angle*.5)))
        inverse = (-parent[0], -parent[1], -parent[2], parent[3])
        result[index] = quaternion_product(inverse,
            quaternion_product(offset, quaternion_product(parent, result[index])))
    return tuple(tuple(q) for q in result)


@dataclass(frozen=True)
class Track:
    kind: int
    times: tuple
    values: tuple

    def sample(self, time_ms):
        if not math.isfinite(time_ms) or time_ms < 0 or not self.times:
            raise ValueError('Invalid animation sample time')
        time_ms = f32(time_ms)
        frame = f32(time_ms*f32(.03))
        index = max(0, bisect_right(self.times, frame)-1)
        # Native compares integer milliseconds, then interpolates using frame
        # times multiplied in float32 and truncated. Frame30 becomes999ms.
        start = int(f32(self.times[index]*f32(1000/30)))
        if index == len(self.times)-1 or int(time_ms) == start:
            return self.values[index]
        end = int(f32(self.times[index+1]*f32(1000/30)))
        if end == start:
            return self.values[index]
        fraction = min(1., max(0., f32(f32(time_ms-start)/(end-start))))
        a, b = self.values[index:index+2]
        if fraction == 0:
            return a
        if fraction == 1:
            return b
        if self.kind == 5:
            return slerp(a, b, fraction)
        # Native position interpolation uses a + (b-a)*fraction.
        return tuple(f32(x+f32(f32(y-x)*fraction)) for x, y in zip(a, b))


class Animation(Resource):
    def __init__(self, data, bindings=None):
        super().__init__(data)
        count = self.word(self.root+36)
        if not 0 < count <= 1024:
            raise ValueError('Invalid collision animation channel count')
        table = self.pointer(self.root+40, count*32)
        self.channels = {}
        for i in range(count):
            record = table+i*32
            if self.word(record+4) != 1 or self.word(record+12) != 1 or self.word(record+20) or self.word(record+28):
                raise ValueError('Unsupported collision animation channel')
            sampler = self.values(self.pointer(record+8, 32), 8, 'I')
            channel = self.pointer(record+16, 12)
            kind, flags = self.values(channel+4, 2, 'I')
            binding = self.string(channel)
            if bindings is not None and binding not in bindings:
                continue
            if kind not in ((1, 5) if bindings is None else (1, 2, 3, 4, 5)) or flags not in ((0x19ffff,) if bindings is None else (0x19ffff, 0x3bffff)):
                raise ValueError('Unsupported collision animation binding')
            dimensions = 3 if kind == 1 else 4 if kind == 5 else 1
            if sampler[:3] != (1, 3, 1) or sampler[4:6] != (6, dimensions) or sampler[7] != dimensions*4 << 16:
                raise ValueError('Unsupported collision animation sample encoding')
            key = (binding, kind)
            if key in self.channels:
                raise ValueError('Duplicate collision animation binding')
            self.channels[key] = sampler[3], sampler[6], dimensions
        chunks = self.pointer(self.root+48, 8)
        count = self.word(chunks)
        if not 0 < count <= 4096:
            raise ValueError('Invalid collision animation chunk count')
        table = self.pointer(chunks+4, count*28)
        self.chunks = []
        for i in range(count):
            record = table+28*i
            start, end, zero1, zero2, offset, size, zero3 = self.values(record, 7, 'I')
            if start >= end or zero1 or zero2 or zero3 or (self.chunks and start != self.chunks[-1][1]):
                raise ValueError('Unsupported collision animation chunk')
            self.take(offset, size)
            self.chunks.append((start, end, offset, size))

    def track(self, binding, kind, time_ms):
        if not math.isfinite(time_ms) or time_ms < self.chunks[0][0] or time_ms > self.chunks[-1][1]:
            raise ValueError('Animation time outside resource')
        chunk = self.chunks[min(bisect_right([c[0] for c in self.chunks], time_ms)-1, len(self.chunks)-1)]
        _, _, offset, size = chunk
        count = self.word(offset)
        if count > (size-4)//8:
            raise ValueError('Invalid animation source count')
        def source(index):
            if index >= count:
                raise ValueError('Invalid animation source index')
            field = offset+4+index*8
            length, relative = self.values(field, 2, 'I')
            start = field+4+relative
            if start < offset+4+count*8 or start+length > offset+size:
                raise ValueError('Animation source outside chunk')
            return self.take(start, length)
        time_index, value_index, dimensions = self.channels[binding, kind]
        times, values = source(time_index), source(value_index)
        if not times or len(times) % 2 or len(values) != len(times)//2*dimensions*4:
            raise ValueError('Mismatched animation key arrays')
        times = struct.unpack('<'+str(len(times)//2)+'H', times)
        if any(a >= b for a, b in zip(times, times[1:])):
            raise ValueError('Unordered animation keys')
        values = tuple(struct.iter_unpack('<'+str(dimensions)+'f', values))
        if not all(math.isfinite(v) for row in values for v in row):
            raise ValueError('Nonfinite animation keys')
        return Track(kind, times, values)


def matrix(position, rotation, scale):
    """Column-major TRS with the native quaternion matrix convention."""
    x, y, z, w = rotation
    xx, yy, zz = (f32(2*f32(v*v)) for v in (x, y, z))
    xy, xz, yz = (f32(2*f32(a*b)) for a, b in ((x, y), (x, z), (y, z)))
    wx, wy, wz = (f32(2*f32(w*v)) for v in (x, y, z))
    columns = ((f32(f32(1-yy)-zz), f32(xy+wz), f32(xz-wy)),
               (f32(xy-wz), f32(f32(1-xx)-zz), f32(yz+wx)),
               (f32(xz+wy), f32(yz-wx), f32(f32(1-xx)-yy)))
    return tuple(tuple(f32(v*s) for v in column) for column, s in zip(columns, scale))+(tuple(position),)


def transform(matrix, vector, translate=True):
    return tuple(f32(f32(f32(f32(matrix[0][i]*vector[0])+f32(matrix[1][i]*vector[1]))+
                          f32(matrix[2][i]*vector[2]))+(matrix[3][i] if translate else 0)) for i in range(3))


def compose(parent, child):
    return tuple(transform(parent, column, i == 3) for i, column in enumerate(child))


@dataclass(frozen=True)
class Node:
    binding: bytes
    name: bytes
    parent: int
    position: tuple
    rotation: tuple
    scale: tuple


class Skeleton(Resource):
    def __init__(self, data):
        super().__init__(data)
        if self.word(self.root+184) != 1:
            raise ValueError('Unsupported collision skeleton scene count')
        scene = self.pointer(self.root+188, 16)
        self.nodes, self.names = [], {}
        visited = set()
        def visit(field, count, parent, depth):
            if count > 1024 or depth > 64 or len(self.nodes)+count > 2048:
                raise ValueError('Invalid collision skeleton hierarchy')
            if not count:
                return
            table = self.pointer(field, count*80)
            for i in range(count):
                record = table+i*80
                if record in visited:
                    raise ValueError('Cyclic collision skeleton hierarchy')
                visited.add(record)
                binding, name = self.string(record), self.string(record+4)
                values = self.values(record+12, 10, 'f')
                if not all(math.isfinite(v) for v in values) or abs(sum(v*v for v in values[3:7])-1) > .001:
                    raise ValueError('Invalid collision skeleton transform')
                if name in self.names:
                    raise ValueError('Duplicate collision skeleton name')
                index = len(self.nodes)
                self.names[name] = index
                self.nodes.append(Node(binding, name, parent, values[:3], values[3:7], values[7:]))
                visit(record+60, self.word(record+56), index, depth+1)
        visit(scene+12, self.word(scene+8), -1, 0)

    def local_pose(self, animation=None, time_ms=0, motion_root=None):
        """Sample private TRS; optional (name, clip_start_ms, XYZ motion flags).

        Translation motion flags preserve the clip-start coordinate on selected
        axes. Rotation extraction and controller blends are separate operations;
        unsupported flags fail instead of silently approximating those paths.
        """
        if motion_root is not None:
            name, start, flags = motion_root
            if animation is None or name not in self.names or flags < 0 or flags & ~7:
                raise ValueError('Unsupported collision root motion')
        result = []
        for node in self.nodes:
            position, rotation = node.position, node.rotation
            if animation is not None:
                if (node.binding, 1) in animation.channels:
                    position = animation.track(node.binding, 1, time_ms).sample(time_ms)
                # Native channel kinds2..4 address individual XYZ position
                # components; getDefaultValue resolves them to node+0xc.
                for axis in range(3):
                    if (node.binding, axis+2) in animation.channels:
                        values = list(position)
                        values[axis] = animation.track(node.binding, axis+2, time_ms).sample(time_ms)[0]
                        position = tuple(values)
                if (node.binding, 5) in animation.channels:
                    rotation = animation.track(node.binding, 5, time_ms).sample(time_ms)
            if motion_root is not None and node.name == name:
                initial = animation.track(node.binding, 1, start).sample(start)
                position = tuple(initial[axis] if flags & (1 << axis) else value
                                 for axis, value in enumerate(position))
            result.append((position, rotation, node.scale))
        return tuple(result)

    def pose(self, animation=None, time_ms=0, motion_root=None):
        result = []
        for node, values in zip(self.nodes, self.local_pose(animation, time_ms, motion_root)):
            local = matrix(*values)
            result.append(local if node.parent < 0 else compose(result[node.parent], local))
        return tuple(result)


def collision_regions(parts, skeleton, bind_pose, pose):
    """Derive capsule geometry from animated bone transforms, in actor space.

    CRagdoll::Init and CPhysicsRagdollComponent::Update define the cylinder
    length and axial placement. This deliberately returns no skeleton or tracks.
    Native body-query radius inflation is part of query geometry, not visuals.
    This scene-axis diagnostic is not a universal original placement oracle:
    slightly non-orthonormal composed matrices require the original normalized
    matrix-to-quaternion conversion, now used by the Go runtime and checked in
    tests/check_collision_runtime.py against original body placement.
    """
    if len(bind_pose) != len(skeleton.nodes) or len(pose) != len(skeleton.nodes):
        raise ValueError('Inconsistent collision skeleton poses')
    result = []
    for ids, bindings, parameters, _ in parts:
        part_id = ids[0]
        first, second = (skeleton.names[name] for name in bindings)
        radius, _, _, multiplier, _, extension = parameters
        difference = tuple(f32(a-b) for a, b in zip(bind_pose[first][3], bind_pose[second][3]))
        length = f32(math.sqrt(f32(sum(f32(v*v) for v in difference))))
        cylinder = max(f32(length-f32(2*radius)), f32(.01))
        half_length = f32(f32(cylinder+f32(2*extension))*.5)
        offset = 0. if part_id == 1 else f32(f32(radius-extension)+f32(cylinder*.5))
        basis = pose[first]
        # Bullet's Y capsule is rotated90deg about local Z. Its symmetric axis
        # therefore follows the animated bone's local X, with reversed endpoints.
        axis_length = math.sqrt(sum(v*v for v in basis[0]))
        if axis_length <= 0:
            raise ValueError('Degenerate collision bone transform')
        axis = tuple(f32(v/axis_length) for v in basis[0])
        center = tuple(f32(p+f32(v*offset)) for p, v in zip(basis[3], axis))
        endpoint = lambda sign: tuple(f32(p+f32(sign*f32(v*half_length))) for p, v in zip(center, axis))
        result.append({'part_id': part_id, 'axis_start': endpoint(-1), 'axis_end': endpoint(1),
                       'radius_world_units': f32(radius+f32(.02)), 'damage_multiplier': multiplier})
    return tuple(result)
