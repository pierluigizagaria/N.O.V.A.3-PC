"""Compile the owner-approved reduced numeric collision kinematics.

Only collision-reference frames, numeric curves and action timing leave this
development reader. No original names, rigs, animation files or graphs are
serialized. Original inputs are read in place and are not runtime dependencies.
"""
import io
import math

from animation_metadata import clips
from authority_data import (actor_animation, animation_clip_start, float32 as f32,
                            numeric_template, ragdoll_settings, remote_player, state_set)
from collision_controller import ControllerGraph, controller_resources
from hitbox_metadata import Animation, Skeleton, actor_resource


def compile_collision(gol, index, archives, weapon_ids):
    skeleton = Skeleton(actor_resource(archives,b'mp_marine_mesh.bdae',False))
    animation = Animation(actor_resource(archives,b'mp_marine_anims.bdae',True))
    states = {s[0]:s for s in numeric_template(gol,index,1061,14,state_set)[1]}
    actor = numeric_template(gol,index,1002,12,actor_animation)
    remote = numeric_template(gol,index,1002,44,remote_player)
    parts = numeric_template(gol,index,4550,140,ragdoll_settings)[0][0][1]
    resources = controller_resources(archives,('remote.atb','remote.stb'))
    graph = ControllerGraph(resources['remote.atb'],resources['remote.stb'])
    times = clips(io.BytesIO(animation.data),0,len(animation.data))
    needed = {skeleton.names[name] for part in parts for name in part[1]}
    for node in tuple(needed):
        while skeleton.nodes[node].parent >= 0:
            node = skeleton.nodes[node].parent
            needed.add(node)
    nodes = sorted(needed)
    if len(nodes) > 20:
        raise ValueError('Reduced collision frame limit exceeded')
    remap = {node:i for i,node in enumerate(nodes)}
    lower = {name for name,_ in actor[6][0]}
    frames = []
    for i in nodes:
        node = skeleton.nodes[i]
        if node.scale != (1.,1.,1.):
            raise ValueError('Unimplemented scaled collision reference')
        frames.append({'parent':remap.get(node.parent,-1), 'position':node.position,
                       'rotation':node.rotation, 'upper_weight':float(node.name not in lower)})
    bind = skeleton.pose()
    regions = []
    for ids,bindings,parameters,_ in parts:
        first,second = [skeleton.names[name] for name in bindings]
        radius,_,_,damage,_,extension = parameters
        delta = [f32(a-b) for a,b in zip(bind[first][3],bind[second][3])]
        length = f32(math.sqrt(f32(sum(f32(v*v) for v in delta))))
        cylinder = max(f32(length-f32(2*radius)),f32(.01))
        regions.append({'part':ids[0],'frame':remap[first], 'radius':f32(radius+f32(.02)),
                        'half_length':f32(f32(cylinder+f32(2*extension))*.5),
                        'offset':0. if ids[0]==1 else f32(f32(radius-extension)+f32(cylinder*.5)),
                        'damage_multiplier':damage})
    selected, motions = {}, []

    def motion(name):
        if name in selected:
            return selected[name]
        state = states[name]
        if len(state[4]) != 1 or state[3][6] & ~7:
            raise ValueError('Unsupported collision motion')
        clip,rate,_ = state[4][0]
        raw,end = times[clip]
        start = animation_clip_start(raw)
        curves = []
        for node_index in nodes:
            node = skeleton.nodes[node_index]
            fields = {}
            for kind,key,default in ((1,'position',node.position),(5,'rotation',node.rotation)):
                if (node.binding,kind) not in animation.channels:
                    fields[key] = [[0,*default]]
                    continue
                # Retain the collision curve's authored interpolation knots,
                # clipped to this action. No source channel/chunk is serialized.
                keys = {start,end}
                for a,b,_,_ in animation.chunks:
                    if b < start or a > end:
                        continue
                    track = animation.track(node.binding,kind,max(start,a))
                    keys.update(int(f32(t*f32(1000/30))) for t in track.times
                                if start <= int(f32(t*f32(1000/30))) <= end)
                values = [[t-start,*animation.track(node.binding,kind,t).sample(t)] for t in sorted(keys)]
                if all(row[1:]==values[0][1:] for row in values):
                    values = values[:1]
                fields[key] = values
            curves.append(fields)
        result = len(motions)
        selected[name] = result
        motions.append({'length_ms':end-start,'rate':rate,'loop':state[2],
                        'root_flags':state[3][6],'curves':curves})
        return result

    locomotion = [motion(state.encode()) for branch in ('Walk','Run')
                  for _,state in graph.settings[branch]['directions']]
    # A target must remain hittable when it holds another catalogue weapon.
    # Pose coverage is independent from implementing that weapon's projectile.
    weapons = []
    for fields,_,actions in remote[2]:
        if fields[0] in weapon_ids:
            weapons.append({'type_id':fields[0],'actions':[motion(name) for name in actions]})
    if {w['type_id'] for w in weapons} != set(weapon_ids):
        raise ValueError('Missing catalogue weapon collision actions')
    aim = [remap[skeleton.names[name]] for name in (b'Bip01',b'Bip01_Spine',b'Bip01_Spine1',
           b'Bip01_Neck',b'Bip01_L_Clavicle',b'Bip01_R_Clavicle')]
    gun = [motion(name) for name in (b'Idle_MachineGun',b'Fire_MachineGun',
            b'MachinegunIdle_Up',b'MachinegunIdle_Down',b'MachinegunRotate')]
    powers=[motion(remote[4][1]),motion(b'ThrowGrenadeEnd'),motion(remote[4][2])]
    return {'frames':frames,'regions':regions,'motions':motions,'weapons':weapons,
            'machinegun_actions':gun,'machinegun_transition_ms':graph.settings['UpperBodyBlend']['duration'],
            'machinegun_angle_range':30.,
            'power_actions':powers,
            'locomotion':locomotion,'aim_frames':aim,
            'walk_speed':graph.settings['Move']['speeds'][0][1],
            'run_speed':graph.settings['Move']['speeds'][1][1],
            'speed_scale':f32(.118750006),'speed_offset':f32(.4),'maximum_speed':f32(1.35),
            'transition_ms':graph.settings['LowerBody']['duration'],'shot_radius':f32(.001)}
