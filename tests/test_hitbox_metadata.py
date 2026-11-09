"""Collision animation derivation: synthetic bounds and original sampler parity."""
import math
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from hitbox_metadata import Animation, Skeleton, Track, actor_resource, aim_rotations, blend_values, collision_regions
from hitbox_metadata import directional_weights, locomotion_weights, track_mask, mixer_weights, additive_values, TransitionBlend
from authority_data import actor_animation, numeric_template, ragdoll_settings, remote_player, state_set, float32, animation_clip_start
from animation_metadata import clips
from collision_controller import ClipClock, ControllerGraph, controller_resources, remote_action, state_restarts
from world_data import object_types, technical_resources, template_index


def resource():
    b = bytearray(600)
    b[:8] = b'BRES\xfe\xff\0\0'
    relocs = (32, 168, 360, 368, 176, 432, 416)
    def words(offset, *values): struct.pack_into('<'+str(len(values))+'I', b, offset, *values)
    words(8, 60, len(b), len(relocs)); words(24, 560); words(32, 128)
    words(164, 1, 352, 0, 428)
    words(352, 0, 1, 384, 1, 416, 0, 0, 0)
    words(384, 1, 3, 1, 0, 6, 3, 1, 12 << 16)
    words(416, 524, 1, 0x19ffff)
    words(428, 1, 436)
    words(436, 0, 1000, 0, 0, 464, 48, 0)
    words(464, 2, 4, 12, 24, 8)
    struct.pack_into('<2H6f', b, 484, 0, 30, 1, 2, 3, 3, 4, 5)
    words(520, 2); b[524:526] = b'n\0'
    words(560, *relocs)
    return b


def skeleton_resource():
    b = bytearray(resource()) + bytearray(400)
    def words(offset, *values): struct.pack_into('<'+str(len(values))+'I', b, offset, *values)
    relocs = (32, 316, 612, 616, 620, 676, 696, 700)
    words(12, len(b), len(relocs)); words(24, 880)
    words(312, 1, 600); words(608, 1, 616)
    words(616, 784, 784, 0); words(672, 1, 696)
    struct.pack_into('<10f', b, 628, 3, 4, 5, 0, 0, math.sqrt(.5), math.sqrt(.5), 1, 1, 1)
    words(696, 800, 800, 0)
    struct.pack_into('<10f', b, 708, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1)
    words(780, 5); b[784:789] = b'root\0'
    words(796, 4); b[800:804] = b'tip\0'
    words(880, *relocs)
    return b


class HitboxMetadata(unittest.TestCase):
    def test_remote_actions_and_state_restarts(self):
        actions = tuple(range(101,108))
        self.assertEqual(remote_action(3,False,actions),(False,103))
        self.assertEqual(remote_action(3,True,actions),(True,104))
        self.assertEqual(remote_action(0,True,actions),(False,101))
        self.assertEqual(remote_action(2,False,actions),(True,102))
        self.assertEqual(remote_action(4,True,actions),(True,None))
        self.assertTrue(state_restarts('idle','idle',0,False))
        self.assertFalse(state_restarts('idle','idle',2,False))
        self.assertTrue(state_restarts('idle','idle',3,False))
        self.assertTrue(state_restarts('idle','idle',2,True))
        self.assertTrue(state_restarts('idle','reload',2,False))

    def test_private_clock_truncates_each_update(self):
        clock = ClipClock(200,100,.8)
        for _ in range(3):
            clock,playing,_,_ = clock.advance(1)
        self.assertEqual(clock.current,0)
        clock,playing,samples,events = clock.advance(125)
        self.assertFalse(playing)
        self.assertEqual((clock.current,samples,events),(99,(299.,),((200.,299.,200.,300.),)))
        self.assertTrue(clock.advance(1)[1])
        loop = ClipClock(200,100,-1,0x1000,90)
        loop,_,samples,events = loop.advance(20)
        self.assertEqual((loop.current,samples,events),(10,(290.,),((200.,210.,200.,300.),(290.,300.,200.,300.))))
        for args in ((0,0,1),(0,100,math.nan),(0,100,1,0,100)):
            with self.assertRaises(ValueError):
                ClipClock(*args)

    def test_private_controller_bounds(self):
        graph = b'<root name="root" type="blend"><animator name="a"/><animator name="b" lock0="true"/></root>'
        settings = b'<statetree><node name="root" time="100"><weight index="0" value="1"/><weight index="1" value="0"/></node><animator name="a" state="idle"/></statetree>'
        parsed = ControllerGraph(graph,settings)
        self.assertEqual(parsed.nodes['root'].children,('a','b'))
        self.assertTrue(parsed.nodes['b'].lock_start)
        self.assertEqual(parsed.settings['root']['duration'],100)
        self.assertEqual(parsed.settings['a']['state'],'idle')
        for cut in range(len(graph)):
            with self.assertRaises(ValueError):
                ControllerGraph(graph[:cut],settings)
        for cut in range(len(settings)):
            with self.assertRaises(ValueError):
                ControllerGraph(graph,settings[:cut])
        for a,b in ((graph.replace(b'name="b"',b'name="a"'),settings),
                    (graph.replace(b'type="blend"',b'type="unknown"'),settings),
                    (graph.replace(b'lock0="true"',b'lock0="maybe"'),settings),
                    (graph,settings.replace(b'time="100"',b'time="-1"')),
                    (graph,settings.replace(b'value="1"',b'value="nan"')),
                    (graph,settings.replace(b'index="1"',b'index="0"')),
                    (graph,settings.replace(b'name="root"',b'name="absent"')),
                    (b'<!DOCTYPE root>'+graph,settings)):
            with self.assertRaises(ValueError):
                ControllerGraph(a,b)

    def test_private_controller_composes_active_masked_poses(self):
        graph = b'<root name="root" type="mixer"><animator name="a"/><node name="action" type="blend"><animator name="b"/><animator name="unused"/></node></root>'
        settings = b'<statetree><node name="root"><filter index="0" value="0" isAdditive="false"/><weight index="0" value="1"/><weight index="1" value="1"/></node><node name="action"><weight index="0" value="1"/></node></statetree>'
        controller = ControllerGraph(graph,settings)
        row = lambda x: ((x,0.,0.),(0.,0.,0.,1.),(1.,1.,1.))
        samples = {'a':(row(1),row(2)),'b':(row(3),row(4))}
        self.assertEqual(controller.evaluate(samples,masks={'root':(0,1)}),(row(1),row(4)))
        self.assertEqual(controller.evaluate(samples,masks={'root':(.5,.5)}),(row(2),row(3)))
        self.assertEqual(controller.evaluate({'a':samples['a']},weights={'root':(1,0)}),samples['a'])
        for supplied,weights,masks in ((samples,{},{}),(samples,{'action':(0,1)},{'root':(0,1)}),
                                      (samples,{'root':(0,0)},{'root':(0,1)})):
            with self.assertRaises(ValueError):
                controller.evaluate(supplied,weights,masks)

    def test_transition_reversal_and_duration(self):
        state = TransitionBlend(100,100,0,0,(1.,0.))
        state = state.advance(1,-1,75)
        self.assertEqual(state.weights, (.25,.75))
        state = state.advance(-1,200,50)
        self.assertEqual((state.elapsed,state.duration,state.weights), (100,200,(.5,.5)))
        state = state.advance(-1,-1,150)
        self.assertEqual((state.elapsed,state.duration,state.direction,state.weights), (0,100,0,(1.,0.)))
        state = state.advance(1,0,0)
        self.assertEqual((state.elapsed,state.duration,state.weights), (100,100,(0.,1.)))
        for args in ((2,100,10),(1,-2,10),(1,100,-1),(1,100,.5)):
            with self.assertRaises(ValueError):
                state.advance(*args)

    def test_locomotion_boundaries_and_validation(self):
        angles = (0.,90.,180.,270.)
        for vector, expected in (((0,1,0), (0,1,(1.,0.))), ((-1,0,0), (1,2,(1.,0.))),
                                 ((0,-1,0), (2,3,(1.,0.))), ((1,0,0), (3,0,(1.,0.))),
                                 ((1,1,0), (3,0,(.5,.5))), ((0,0,0), (0,1,(1.,0.)))):
            self.assertEqual(directional_weights(vector, angles), expected)
        for vector, invalid in (((0,math.nan,0), angles), ((0,1), angles),
                                ((0,1,0), (0,0)), ((0,1,0), (0,360))):
            with self.assertRaises(ValueError):
                directional_weights(vector, invalid)
        thresholds, lengths, times = (.8,1.2), (1200,800), (300,600)
        self.assertEqual(locomotion_weights(.4,.4,thresholds,lengths,times), ((1.,0.), ((0,.5,.25),)))
        self.assertEqual(locomotion_weights(1.1,1.35,thresholds,lengths,times), ((0.,1.), ((1,1.125,.75),)))
        for args in ((math.nan,1,thresholds,lengths,times), (1,-1,thresholds,lengths,times),
                     (1,1,(0,1),lengths,times), (1,1,thresholds,(0,800),times),
                     (1,1,thresholds,lengths,(-1,600))):
            with self.assertRaises(ValueError):
                locomotion_weights(*args)

    def test_aim_rotation_wrap_and_validation(self):
        identity = [(0.,0.,0.,1.)]*6
        expected = aim_rotations(identity, 90)
        self.assertEqual(expected, aim_rotations(identity, -270))
        self.assertEqual(aim_rotations(identity, 0), tuple(identity))
        for index, angle in enumerate((0,36,18,18,18,18)):
            for actual, value in zip(expected[index], (math.sin(math.radians(angle)*.5), 0, 0, math.cos(math.radians(angle)*.5))):
                self.assertAlmostEqual(actual, value, places=5)
        for value in (math.nan, math.inf, 361):
            with self.assertRaises(ValueError):
                aim_rotations(identity, value)
        for rotations in ([], identity[:5], [(0,0,0,0)]*6):
            with self.assertRaises(ValueError):
                aim_rotations(rotations, 0)

    def test_active_track_blending(self):
        self.assertEqual(blend_values(1, [(1,2,3)], [.2]), (1,2,3))
        self.assertEqual(blend_values(1, [(1,2,3),(3,4,5)], [.25,.75]), (2.5,3.5,4.5))
        self.assertEqual(blend_values(1, [(1,2,3),(3,4,5)], [1,1]), (4,6,8))
        result = blend_values(5, [(0,0,0,1),(0,0,1,0)], [.5,.5])
        for actual, expected in zip(result, (0,0,math.sqrt(.5),math.sqrt(.5))):
            self.assertAlmostEqual(actual, expected, places=6)
        for kind, values, weights in ((2, [(1,2,3)], [1]), (1, [], []),
            (1, [(1,2,3)], []), (1, [(1,2)], [1]), (1, [(1,2,math.nan)], [1]),
            (1, [(1,2,3)], [0]), (1, [(1,2,3)], [-1]), (1, [(1,2,3)], [math.inf])):
            with self.assertRaises(ValueError):
                blend_values(kind, values, weights)
        self.assertEqual(mixer_weights(1,(.1,.75)),(.25,.75))
        self.assertEqual(mixer_weights(0,(1,1)),(1,0))
        for mask in (math.nan,-1,2):
            with self.assertRaises(ValueError):
                mixer_weights(mask,(1,1))
        self.assertEqual(additive_values(1,[(1,2,3),(3,4,5)],[-1,1]),(2,2,2))
        self.assertEqual(additive_values(5,[],[]),(0,0,0,1))
        self.assertEqual(additive_values(1,[(1,2,3)],[-1]),(1,2,3))
        self.assertEqual(additive_values(5,[(0,0,0,-1)],[-1]),(0,0,0,1))

    def test_root_motion_uses_clip_start_not_zero(self):
        skeleton = Skeleton(skeleton_resource())
        class Moving:
            channels = {(b'root', 1)}
            def track(self, binding, kind, time):
                self_check = binding == b'root' and kind == 1
                if not self_check:
                    raise ValueError('Unexpected synthetic track')
                return Track(1, (0, 30), ((11., 22., 33.), (13., 26., 39.)))
        for flags in range(8):
            local = skeleton.local_pose(Moving(), 999, (b'root', 0, flags))
            self.assertEqual(local[0][0], tuple((11.,22.,33.)[i] if flags & (1 << i)
                                              else (13.,26.,39.)[i] for i in range(3)))
        for root in ((b'missing', 0, 3), (b'root', 0, 8), (b'root', 0, -1)):
            with self.assertRaises(ValueError):
                skeleton.local_pose(Moving(), 999, root)

    def test_animated_bone_capsule_geometry(self):
        skeleton = Skeleton(skeleton_resource())
        pose = skeleton.pose()
        for actual, expected in zip(pose[1][3], (3, 5, 5)):
            self.assertAlmostEqual(actual, expected, places=6)
        part = ((2, 0, 1), (b'root', b'tip'), (.2, 1, 2, 1, 1, .1), False)
        region = collision_regions([part], skeleton, pose, pose)[0]
        for actual, expected in zip(region['axis_start'], (3, 4, 5)):
            self.assertAlmostEqual(actual, expected, places=6)
        for actual, expected in zip(region['axis_end'], (3, 4.8, 5)):
            self.assertAlmostEqual(actual, expected, places=6)
        self.assertAlmostEqual(region['radius_world_units'], .22, places=6)
        # The pelvis is centered on its bone, not offset toward the next bone.
        pelvis = ((1, 0, 0), part[1], part[2], False)
        region = collision_regions([pelvis], skeleton, pose, pose)[0]
        self.assertAlmostEqual(region['axis_start'][1], 3.6, places=6)
        self.assertAlmostEqual(region['axis_end'][1], 4.4, places=6)
        for offset, value in ((312, 2), (608, 2049), (676, 616), (700, 784)):
            b = skeleton_resource(); struct.pack_into('<I', b, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                Skeleton(b)

    def test_native_integer_frame_timing(self):
        a = Animation(resource())
        track = a.track(b'n', 1, 500)
        self.assertEqual(track.times, (0, 30))
        self.assertEqual(track.sample(0), (1., 2., 3.))
        self.assertEqual(track.sample(999), (3., 4., 5.))
        for actual, expected in zip(track.sample(500), (2.00100112, 3.00100088, 4.00100088)):
            self.assertAlmostEqual(actual, expected, places=6)
        q = Track(5, (0, 30), ((0., 0., 0., 1.), (0., 0., 1., 0.)))
        for actual, expected in zip(q.sample(500), (0., 0., .707662523, .706550658)):
            self.assertAlmostEqual(actual, expected, places=6)
        flipped = Track(5, (0, 30), ((0., 0., 0., -1.), (0., 0., 0., 1.)))
        self.assertEqual(flipped.sample(500), (0., 0., 0., 1.))

    def test_bounds_and_corruption(self):
        b = resource()
        for cut in range(len(b)):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                Animation(b[:cut])
        for field, value in ((8, 61), (16, 1000), (24, 599), (164, 1025),
                             (168, 590), (356, 2), (384, 0), (420, 99),
                             (428, 4097), (444, 1), (452, 599), (464, 1000),
                             (468, 2), (472, 600), (476, 16), (484, 0)):
            broken = bytearray(b); struct.pack_into('<I', broken, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                Animation(broken).track(b'n', 1, 500)
        for time in (-1, math.nan, math.inf, 1001):
            with self.assertRaises(ValueError):
                Animation(b).track(b'n', 1, time)

    def test_original_samples_match_native_consumer(self):
        library = ROOT / 'local/input/lib/x86/libNOVA3_neon.so'
        archives = sorted((ROOT / 'local/game/obb').glob('*.obb'))
        oracle = ROOT / 'build/win32/tests/Release/native-world-reader-tests.exe'
        if os.environ.get('NOVA_REAL_TESTS') != '1' or not library.is_file() or not archives or not oracle.is_file():
            self.skipTest('Original hitbox animation inputs are opt-in')
        animation = Animation(actor_resource(archives, b'mp_marine_anims.bdae', True))
        self.assertEqual((len(animation.channels), len(animation.chunks)), (76, 93))
        skeleton = Skeleton(actor_resource(archives, b'mp_marine_mesh.bdae', False))
        self.assertEqual(len(skeleton.nodes), 80)
        gol = technical_resources(archives)['gol.bin']
        index = template_index(gol, oracle, library)
        self.assertEqual(object_types(gol)[1002].references, (1061,))
        local_states = numeric_template(gol, index, 1002, 14, state_set)
        inherited_states = numeric_template(gol, index, 1061, 14, state_set)
        self.assertEqual(local_states[1], ())
        self.assertEqual((len(inherited_states[1]), inherited_states[2]), (100, b'remote.stb'))
        self.assertEqual(sum(len(state[4])==1 for state in inherited_states[1]),99)
        self.assertEqual(sum(len(state[4])==0 for state in inherited_states[1]),1)
        self.assertEqual(sum(state[3][6]==3 for state in inherited_states[1]),8)
        self.assertTrue(all(state[3][6] in (0,3) and state[3][5]==0 for state in inherited_states[1]))
        remote = numeric_template(gol, index, 1002, 44, remote_player)
        self.assertEqual(len(remote[2]), 14)
        actor = numeric_template(gol, index, 1002, 12, actor_animation)
        self.assertEqual((actor[0].lower(), actor[4], len(actor[6])),
                         (b'mp_marine_anims.bdae', b'remote.atb', 2))
        resources = controller_resources(archives,('remote.atb','remote.stb'))
        graph = ControllerGraph(resources['remote.atb'],resources['remote.stb'])
        self.assertEqual(len(graph.nodes),25)
        self.assertEqual(graph.nodes[graph.root].children,('BodyMixer','NonInterruptibleRoot'))
        self.assertEqual(graph.settings['BodyMixer']['filter'],(0,0,False))
        self.assertEqual(graph.settings['Move']['speeds'],[('Walk',.8),('Run',1.2)])
        names = {s[0] for s in inherited_states[1]}
        for settings in graph.settings.values():
            if 'state' in settings:
                self.assertIn(settings['state'].encode(),names)
            for _, state in settings.get('directions',()):
                self.assertIn(state.encode(),names)
        # Lower-body filtering excludes exact nodes from the upper action layer.
        # Spine descendants are individually named; no recursive mask is implied.
        lower = {name for name,_ in actor[6][0]}
        mask = track_mask(tuple(node.name for node in skeleton.nodes),lower,0)
        self.assertEqual(mask[skeleton.names[b'Bip01']],0)
        self.assertEqual(mask[skeleton.names[b'Bip01_Pelvis']],0)
        self.assertEqual(mask[skeleton.names[b'Bip01_Head']],1)
        # Not all weapon presentation references are supported actor states;
        # this verifies the basic assault-rifle action set without inventing
        # handling for optional vehicles or unused original entries.
        self.assertTrue(all(name in names for name in remote[2][0][2]))
        parts = numeric_template(gol, index, 4550, 140, ragdoll_settings)[0][0][1]
        bind_pose = skeleton.pose()
        first = collision_regions(parts, skeleton, bind_pose, skeleton.pose(animation, 0))
        moved = collision_regions(parts, skeleton, bind_pose, skeleton.pose(animation, 6500))
        self.assertEqual(len(first), 11)
        self.assertEqual(first[2]['damage_multiplier'], 3)
        self.assertNotEqual(first[2]['axis_start'], moved[2]['axis_start'])
        # Authored root motion is still present here. This is a private geometry
        # derivation, not a complete replicated actor pose or usable static fallback.
        self.assertGreater(abs(moved[2]['axis_start'][1]-first[2]['axis_start'][1]), 1)
        clip_times = clips(io.BytesIO(animation.data), 0, len(animation.data))
        run_state = next(state for state in inherited_states[1] if state[0] == b'Run')
        self.assertEqual(run_state[3][6], 3)
        start, end = clip_times[run_state[4][0][0]]
        start = animation_clip_start(start)
        root = (remote[4][0], start, run_state[3][6])
        # Original scene-node matrix construction and hierarchy composition:
        # this is independent from the Python implementation under test.
        pose_queries = [(None, 0, None), (animation, 0, None), (animation, 6500, None),
                        (animation, start, root), (animation, (start+end)/2, root),
                        (animation, end-1, root)]
        poses, body = [], bytearray(struct.pack('<I', len(pose_queries)*len(skeleton.nodes)))
        for sequence, (source, time, motion) in enumerate(pose_queries):
            poses.extend(skeleton.pose(source, time, motion))
            for node, (p, q, scale) in zip(skeleton.nodes, skeleton.local_pose(source, time, motion)):
                parent = node.parent + sequence*len(skeleton.nodes) if node.parent >= 0 else -1
                body.extend(struct.pack('<i10f', parent, *p, *q, *scale))
        run = subprocess.run([str(oracle), str(library), '--compose-hitbox-pose'], input=body, capture_output=True, timeout=90)
        self.assertEqual(run.returncode, 0, run.stderr.decode(errors='replace')[-1000:])
        rows = run.stdout.decode().split('HITBOX_POSE 1')[-1].strip().splitlines()
        self.assertEqual(len(rows), len(poses))
        for row, pose in zip(rows, poses):
            native = [float(v) for v in row.split()[1:]]
            for expected, actual in zip(native, (v for column in pose for v in column)):
                self.assertTrue(math.isclose(actual, expected, rel_tol=4e-6, abs_tol=4e-6),
                                (row, actual, expected))
        local_poses = [skeleton.local_pose(source, time, motion) for source, time, motion in pose_queries]
        try:
            from original_pose_oracle import OriginalPoseOracle
            aim_oracle = OriginalPoseOracle(library)
        except ImportError:
            self.fail('Opt-in pose parity requires tests/original_pose_requirements.txt')
        for raw_start,end in clip_times.values():
            aligned = animation_clip_start(raw_start)
            self.assertEqual(aim_oracle.clip_bounds(raw_start,end),(end-aligned,aligned,end))
        states = ((b'first',.8,True,0),(b'second',1.5,False,3),(b'first',1.,False,0))
        requests = ((0,0,False,0),(0,0,False,300),(0,2,False,400),(0,2,True,666),
                    (1,2,False,222),(2,2,False,333),(0,2,False,444),(0,1,False,555))
        for factor in (.8,1.,1.25):
            previous = None
            for request,(calls,time) in zip(requests,aim_oracle.state_playback(states,requests,factor)):
                selected,flags,finished,prior_time = request
                name,speed,loop,play_flags = states[selected]
                restart = state_restarts(previous,name,flags,finished)
                expected = (('play',0,100+selected,play_flags),) if restart else ()
                expected += (('speed',0,float32(float32(speed)*float32(factor))),('loop',0,loop))
                self.assertEqual(calls,expected)
                self.assertEqual(time,0 if restart else prior_time)
                previous = name
        actions = tuple(range(101,108))
        events = (0,2,3,3,1,3,2,0,-1,4,999)
        for initial in (False,True):
            aimed,slot = initial,0
            for event,(native_aimed,calls) in zip(events,aim_oracle.remote_states(events,actions,initial)):
                aimed,selected = remote_action(event,aimed,actions)
                self.assertEqual(aimed,native_aimed)
                if selected is None:
                    self.assertEqual(calls,())
                else:
                    slot = 1-slot
                    self.assertEqual(calls,((0x5d5800,slot,selected,0,-1),
                                            (0x5d22b0,5,1 if slot else -1,-1,0)))
        deltas = (0,1,16,33,67,1000,2)
        for start,length in ((0,100),(999,666)):
            for speed in (0.,.8,1.,1.25,-.8,-1.):
                for flags in (0,0x1000,0x200,0x400,0x40,0x800,0x3000):
                    native = aim_oracle.clock(start,length,speed,flags,0,deltas)
                    clock = ClipClock(start,length,speed,flags)
                    for delta,expected in zip(deltas,native):
                        clock,playing,samples,events = clock.advance(delta)
                        self.assertEqual((clock.current,clock.previous,clock.flags,playing,samples,events),expected,
                                         (start,length,speed,flags,delta))
        for node in range(len(skeleton.nodes)):
            for kind,field in ((1,0),(5,1)):
                for indices,weights in (((1,),(-1.,)),((1,3),(-1.,1.)),((1,2,4),(.25,-.5,1.))):
                    values = [local_poses[i][node][field] for i in indices]
                    native = aim_oracle.added_value(kind,values,weights)
                    actual = additive_values(kind,values,weights)
                    for a,b in zip(actual,native):
                        self.assertAlmostEqual(a,b,places=5,msg=str((node,kind,weights,a,b)))
        for mask in (0.,.25,.5,.75,1.):
            for weights in ((1.,1.),(.1,.8),(0.,0.),(.3,0.)):
                for additive in (False,True):
                    self.assertEqual(mixer_weights(mask,weights,additive),
                                     aim_oracle.mixer_weights(mask,weights,additive))
        # Multiple channels can target the same node; both must receive its mask.
        # Filters are stored per channel, including across the32-bit boundary.
        targets = tuple(1+i//2 for i in range(64))
        for selected in ((),(1,),(16,17,32),(1,7,16,31,32,99),tuple(range(1,33))):
            for selector in (0,1):
                values, filters = aim_oracle.track_mask(targets,selected,selector)
                self.assertEqual(values,track_mask(targets,selected,selector))
                self.assertEqual(filters,(0,sum(1<<i for i,v in enumerate(values) if v==1),
                                         sum(1<<i for i,v in enumerate(values) if v==0)))
        for source, time, motion in pose_queries:
            pose = skeleton.pose(source,time,motion)
            regions = collision_regions(parts,skeleton,bind_pose,pose)
            for part, region in zip(parts,regions):
                first, second = [skeleton.names[name] for name in part[1]]
                radius, extension = part[2][0], part[2][5]
                difference = [float32(a-b) for a,b in zip(bind_pose[first][3],bind_pose[second][3])]
                length = float32(math.sqrt(float32(sum(float32(v*v) for v in difference))))
                cylinder = max(float32(length-float32(2*radius)),float32(.01))
                matrix = [v for index,column in enumerate(pose[first]) for v in (*column,float(index==3))]
                position, rotation = aim_oracle.body_transform(matrix,part[0][0],radius,extension,cylinder)
                for a,b,c in zip(position,region['axis_start'],region['axis_end']):
                    self.assertAlmostEqual(a,(b+c)*.5,places=5)
                x,y,z,w = rotation
                # Physics capsules follow rotated local Y; endpoint order is
                # symmetric, while the original placement rotates Y toward-X.
                axis = (2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w))
                half = float32(float32(cylinder+float32(2*extension))*.5)
                for i in range(3):
                    self.assertAlmostEqual(position[i]+axis[i]*half,region['axis_start'][i],places=5)
                    self.assertAlmostEqual(position[i]-axis[i]*half,region['axis_end'][i],places=5)
        transitions = ((1,-1,33),(1,-1,33),(-1,200,20),(-1,-1,200),
                       (1,0,0),(0,-1,30),(-1,50,25),(1,80,60),(1,-1,100))
        for duration in (0,100,333):
            for weights in ((1.,0.),(.25,.75),(0.,1.)):
                native = aim_oracle.transition(duration,weights,transitions)
                state = TransitionBlend(duration,duration,0,0,weights)
                for update, expected in zip(transitions,native):
                    state = state.advance(*update)
                    self.assertEqual((state.duration,state.elapsed,state.direction), expected[:3])
                    for a, b in zip(state.weights,expected[3]):
                        self.assertAlmostEqual(a,b,places=7)
        vectors = [(0.,0.,0.)] + [(math.sin(math.radians(angle)), math.cos(math.radians(angle)), z)
                                 for angle in range(0,360,5) for z in (0.,.3,-.7)]
        for vector in vectors:
            native = aim_oracle.direction(vector)
            actual = directional_weights(vector, (0.,90.,180.,270.))
            self.assertEqual(actual[:2], native[:2], vector)
            for a, b in zip(actual[2], native[2]):
                self.assertAlmostEqual(a, b, places=6, msg=str(vector))
        for thresholds in ((.8,1.2), (.5,2.)):
            for lengths, times in (((1200,800),(300,600)), ((600,1800),(600,1350))):
                for previous in (.4,.8,1.,1.1,1.2,1.35,2.):
                    for current in (0.,.4,.5,.8,1.,1.1,1.2,1.35,2.,3.):
                        native = aim_oracle.speed(previous,current,lengths,times,thresholds)
                        actual = locomotion_weights(previous,current,thresholds,lengths,times)
                        self.assertEqual(len(actual[1]), len(native[1]))
                        for a, b in zip((*actual[0], *(v for row in actual[1] for v in row)),
                                        (*native[0], *(v for row in native[1] for v in row))):
                            self.assertAlmostEqual(a, b, places=6,
                                                   msg=str((previous,current,thresholds,lengths,times)))
        aim_indices = [skeleton.names[name] for name in (b'Bip01', b'Bip01_Spine', b'Bip01_Spine1',
                       b'Bip01_Neck', b'Bip01_L_Clavicle', b'Bip01_R_Clavicle')]
        for local in local_poses:
            rotations = [local[i][1] for i in aim_indices]
            for pitch in (-179., -90., -20., 0., 45., 90., 180., 270., 359.):
                native = aim_oracle.adjust(rotations, pitch)
                for expected, actual in zip(native, aim_rotations(rotations, pitch)):
                    for a, b in zip(actual, expected):
                        self.assertTrue(math.isclose(a, b, rel_tol=7e-6, abs_tol=7e-6), (pitch, a, b))
        blend_queries = []
        for node in range(len(skeleton.nodes)):
            for kind, field in ((1, 0), (5, 1)):
                for indices, weights in (((1,), (.25,)), ((2,4), (.25,.75)),
                                         ((1,3,5), (.2,.3,.5))):
                    blend_queries.append((kind, [local_poses[i][node][field] for i in indices], weights))
        body = bytearray(struct.pack('<I', len(blend_queries)))
        for kind, values, weights in blend_queries:
            body.extend(struct.pack('<2I', kind, len(weights)))
            scalars = [*weights, *(v for row in values for v in row)]
            body.extend(struct.pack('<'+str(len(scalars))+'f', *scalars))
        run = subprocess.run([str(oracle), str(library), '--blend-hitbox-values'], input=body, capture_output=True, timeout=90)
        self.assertEqual(run.returncode, 0, run.stderr.decode(errors='replace')[-1000:])
        rows = run.stdout.decode().split('HITBOX_BLENDS 1')[-1].strip().splitlines()
        self.assertEqual(len(rows), len(blend_queries))
        for row, (kind, values, weights) in zip(rows, blend_queries):
            for expected, actual in zip(map(float, row.split()[1:]), blend_values(kind, values, weights)):
                self.assertTrue(math.isclose(actual, expected, rel_tol=4e-6, abs_tol=4e-6),
                                (row, actual, expected, kind, weights))
        queries = []
        # Validate every source boundary, retaining only selected numeric
        # samples for the native oracle. No source artifact is written to disk.
        for chunk_index, (start, end, _, _) in enumerate(animation.chunks):
            for channel_index, (binding, kind) in enumerate(animation.channels):
                track = animation.track(binding, kind, (start+end)/2)
                for value in track.sample((start+end)/2):
                    self.assertTrue(math.isfinite(value))
                if channel_index % 13 == chunk_index % 13:
                    for time in (start, (start+end)/2+.125, end-.125):
                        queries.append((track, time))
        body = bytearray(struct.pack('<I', len(queries)))
        for track, time in queries:
            body.extend(struct.pack('<IIf', track.kind, len(track.times), time))
            body.extend(struct.pack('<'+str(len(track.times))+'H', *track.times))
            values = [v for row in track.values for v in row]
            body.extend(struct.pack('<'+str(len(values))+'f', *values))
        run = subprocess.run([str(oracle), str(library), '--sample-hitbox-tracks'], input=body, capture_output=True, timeout=90)
        self.assertEqual(run.returncode, 0, run.stderr.decode(errors='replace')[-1000:])
        rows = run.stdout.decode().split('HITBOX_SAMPLES 1')[-1].strip().splitlines()
        rows = [r for r in rows if r.strip()]
        self.assertEqual(len(rows), len(queries))
        for row, (track, time) in zip(rows, queries):
            native = [float(v) for v in row.split()[1:]]
            for expected, actual in zip(native, track.sample(time)):
                self.assertTrue(math.isclose(actual, expected, rel_tol=3e-6, abs_tol=3e-6),
                                (row, time, track.kind, actual, expected))


if __name__ == '__main__':
    unittest.main()
