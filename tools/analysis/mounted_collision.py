"""Derive vehicle collision geometry without exporting scene resources."""
import math
import io

from authority_data import numeric_template, target_bodies, remote_mech, state_set, animation_clip_start, float32 as f32
from hitbox_metadata import Animation, Skeleton, actor_resource, compose, transform
from animation_metadata import clips
from world_data import DictionaryStream, object_types


def offset_matrix(position, rotation):
    x, y, z = (v * math.pi / 180 for v in rotation)
    sx, sy, sz = math.sin(x), math.sin(y), math.sin(z)
    cx, cy, cz = math.cos(x), math.cos(y), math.cos(z)
    return ((f32(cz*cy), f32(sz*cy), f32(-sy)),
            (f32(sy*sx*cz-cx*sz), f32(sy*sx*sz+cx*cz), f32(cy*sx)),
            (f32(sy*cx*cz+sz*sx), f32(sy*cx*sz-cz*sx), f32(cy*cx)),
            tuple(position))


def rigid_capsules(gol, index, archives, kind):
    reader = DictionaryStream(gol); reader.pos = index[kind, 1][0]-9
    resource = reader.string()
    try:
        source = actor_resource(archives, resource, False)
    except ValueError:
        source = actor_resource(archives, resource, True)
    skeleton = Skeleton(source)
    pose = skeleton.pose()
    result = []
    for parameters, _, binding in numeric_template(gol, index, kind, 141, target_bodies):
        height, radius = parameters[:2]
        matrix = offset_matrix(parameters[2:5], parameters[5:8])
        if binding:
            if binding not in skeleton.names:
                raise ValueError('Missing vehicle collision reference')
            matrix = compose(pose[skeleton.names[binding]], matrix)
        # CPhysicsCollisionBody uses Z capsules, including its .02 margin.
        a, b = transform(matrix, (0, 0, -height*.5)), transform(matrix, (0, 0, height*.5))
        result.append({'a':a, 'b':b, 'radius':f32(radius+f32(.02)),
                       'damage_multiplier':max(parameters[9],0)})
    return result


def car_occupants(gol, index, archives, kind):
    """Original target proxies attached by CVehicleCar::UpdateMeshes2."""
    reader = DictionaryStream(gol); reader.pos = index[kind, 1][0]-9
    source = actor_resource(archives, reader.string(), True)
    skeleton = Skeleton(source)
    pose = skeleton.pose()
    driver = skeleton.names[b'dummy_driver']
    platform = skeleton.names[b'dummy_platform_shooter']
    shooter = skeleton.names[b'dummy_shooter']
    if skeleton.nodes[shooter].parent != platform:
        raise ValueError('Unsupported car occupant attachment hierarchy')
    for node in (driver, platform, shooter):
        current = node
        while current >= 0:
            frame = skeleton.nodes[current]
            if frame.scale != (1.,1.,1.) or frame.rotation != (0.,0.,0.,1.):
                raise ValueError('Unsupported car occupant bind transform')
            current = frame.parent

    def proxy(kind):
        reader.pos = index[kind,0][0]-9
        reader.integer()
        if reader.floats(9)[6:] != (1.,1.,1.):
            raise ValueError('Unsupported scaled car proxy root')
        reader.pos = index[kind, 1][0]-9
        resource = actor_resource(archives, reader.string(), False)
        result = []
        for values, _, binding in numeric_template(gol,index,kind,141,target_bodies):
            # The supported proxy's obsolete binding is absent from its whole
            # scene resource. Native getSceneNodeFromName returns null and
            # CPhysicsCollisionBody::Update falls back to the object root,
            # retaining its full-transform flag. This is the original proxy,
            # not a replacement marine standing capsule.
            if not binding or binding in resource:
                raise ValueError('Revisit car proxy collision binding')
            matrix = offset_matrix(values[2:5],values[5:8])
            result.append({'a':transform(matrix,(0,0,-values[0]*.5)),
                           'b':transform(matrix,(0,0,values[0]*.5)),
                           'radius':f32(values[1]+f32(.02)),
                           'damage_multiplier':max(values[9],0)})
        return result

    return {'driver_position':pose[driver][3],
            'shooter_pivot':pose[platform][3],
            'shooter_offset':skeleton.nodes[shooter].position,
            'driver_capsules':proxy(7011),'shooter_capsules':proxy(7012)}


def mech_collision(gol, index, archives, kind=7007):
    """Only twelve collision frames, five numeric capsules and needed curves."""
    skeleton = Skeleton(actor_resource(archives, b'mech_mesh.bdae', False))
    parts = numeric_template(gol, index, kind, 141, target_bodies)
    needed = {skeleton.names[binding] for _, _, binding in parts}
    for node in tuple(needed):
        while skeleton.nodes[node].parent >= 0:
            node = skeleton.nodes[node].parent
            needed.add(node)
    nodes = sorted(needed)
    if len(nodes) > 20:
        raise ValueError('Mounted collision frame limit exceeded')
    remap = {node:i for i,node in enumerate(nodes)}
    animation = Animation(actor_resource(archives, b'mech_anim.bdae', True),
                          {skeleton.nodes[n].binding for n in nodes})
    states = {}
    for reference in (*object_types(gol)[kind].references, kind):
        if (reference,14) in index:
            states.update({s[0]:s for s in numeric_template(gol,index,reference,14,state_set)[1]})
    times = clips(io.BytesIO(animation.data),0,len(animation.data))
    selected = (b'AI_Idle', b'MP_Deactivated', b'MP_GetUp', b'MP_GetDown',
                b'MP_Walk', b'MP_Walk_Back', b'MP_Strafe_Left', b'MP_Strafe_Right',
                b'MP_Fire', b'MP_MissileFireL', b'MP_MissileFireR',
                b'MP_LeftTurn_15', b'MP_RightTurn_15', b'MP_LeftTurn', b'MP_RightTurn',
                b'MP_LeftTurn_90', b'MP_RightTurn_90', b'MP_LeftTurn_135', b'MP_RightTurn_135')
    frames = []
    for n in nodes:
        node = skeleton.nodes[n]
        if node.scale != (1.,1.,1.):
            raise ValueError('Unsupported scaled mounted reference')
        frames.append({'parent':remap.get(node.parent,-1),'position':node.position,
                       'rotation':node.rotation,'upper_weight':0})
    regions = []
    for values, _, binding in parts:
        matrix = offset_matrix(values[2:5],values[5:8])
        regions.append({'frame':remap[skeleton.names[binding]],
                        'a':transform(matrix,(0,0,-values[0]*.5)),
                        'b':transform(matrix,(0,0,values[0]*.5)),
                        'radius':f32(values[1]+f32(.02)),
                        'damage_multiplier':max(values[9],0)})
    motions = []
    for name in selected:
        state = states[name]
        if len(state[4]) != 1 or state[3][6] & ~0x23:
            raise ValueError('Unsupported mounted collision action')
        clip, rate, _ = state[4][0]
        raw, end = times[clip]
        start = animation_clip_start(raw)
        curves = []
        for n in nodes:
            node = skeleton.nodes[n]
            keys = {start,end}
            for a,b,_,_ in animation.chunks:
                if b < start or a > end: continue
                for channel in range(1,6):
                    if (node.binding,channel) not in animation.channels: continue
                    track = animation.track(node.binding,channel,max(start,a))
                    keys.update(int(f32(t*f32(1000/30))) for t in track.times
                                if start <= int(f32(t*f32(1000/30))) <= end)
            position, rotation = [], []
            for time in sorted(keys):
                p, q = node.position, node.rotation
                if (node.binding,1) in animation.channels:
                    p = animation.track(node.binding,1,time).sample(time)
                for axis in range(3):
                    if (node.binding,axis+2) in animation.channels:
                        p = list(p);p[axis]=animation.track(node.binding,axis+2,time).sample(time)[0]
                if (node.binding,5) in animation.channels:
                    q = animation.track(node.binding,5,time).sample(time)
                position.append([time-start,*p]);rotation.append([time-start,*q])
            for values in (position,rotation):
                if all(row[1:]==values[0][1:] for row in values): values[:]=values[:1]
            curves.append({'position':position,'rotation':rotation})
        motions.append({'length_ms':end-start,'rate':rate,'loop':state[2],
                        'root_flags':state[3][6],'curves':curves})
    data = numeric_template(gol,index,kind,160,remote_mech)
    return {'type_id':kind,'frames':frames,'regions':regions,'motions':motions,
            'motion_frame':remap[skeleton.names[b'Bip01']],
            'aim_frame':remap[skeleton.names[data[2][4]]]}


def interaction_capsules(gol, index, archives, kind, category, mech):
    """CInteractiveComponent::ReInit/CheckInteract, without physics margins."""
    reader = DictionaryStream(gol); reader.pos = index[kind, 69][0]-9
    rows = [(reader.floats(8), reader.string()) for _ in range(reader.integer())]
    reader.pos = index[kind, 1][0]-9
    resource = reader.string()
    skeleton = Skeleton(actor_resource(archives, resource, True, pack='weapons_stream.gla')
                        if category == 3 else actor_resource(archives, resource, category == 1))
    pose = skeleton.pose()
    result = []
    for values, binding in rows:
        height, radius = values[:2]
        matrix = offset_matrix(values[2:5], values[5:8])
        frame = -1
        if binding:
            if category == 2:
                if binding != b'Bip01':
                    raise ValueError('Unsupported moving interaction attachment')
                frame = mech['motion_frame']
            else:
                matrix = compose(pose[skeleton.names[binding]], matrix)
        result.append({'frame':frame, 'a':transform(matrix,(0,0,-height*.5)),
                       'b':transform(matrix,(0,0,height*.5)), 'radius':radius,
                       'damage_multiplier':0})
    if not 1 <= len(result) <= 16:
        raise ValueError('Missing multiplayer mount interaction volumes')
    return result


def gunner_anchors(gol,index,archives,kind,position,rotation):
    reader=DictionaryStream(gol);reader.pos=index[kind,67][0]-9
    reader.integer();pivot=reader.string();reader.string();reader.floats(3);seat=reader.string()
    reader.pos=index[kind,1][0]-9;resource=reader.string()
    skeleton=Skeleton(actor_resource(archives,resource,True,pack='weapons_stream.gla'))
    pose=skeleton.pose();placement=offset_matrix(position,rotation)
    first=compose(placement,pose[skeleton.names[pivot]])
    second=compose(placement,pose[skeleton.names[seat]])
    y=math.asin(max(-1,min(1,-second[0][2])))
    if abs(math.cos(y))<.0001:raise ValueError('Unsupported gunner anchor pole')
    euler=[math.atan2(second[1][2],second[2][2]),y,math.atan2(second[0][1],second[0][0])]
    return {'pivot':first[3],'seat':second[3],
            'rotation':[f32(v*180/math.pi) for v in euler]}
