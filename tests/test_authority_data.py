"""Authority readers: synthetic corruption tests plus optional native evidence."""
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from authority_data import actor_animation, actor_collision, damage, firing_interval, grenade, health, multiplayer_loadout, numeric_template, physics_ragdoll, player_body, projectile, ragdoll_settings, reload_timing, remote_player, state_set, weapon, weapon_addons, weapon_template
from world_data import DictionaryStream, technical_resources, template_index


def fixture():
    data = bytearray()
    def ints(*values): data.extend(struct.pack('>' + 'i' * len(values), *values))
    def floats(count): data.extend(struct.pack('>' + 'f' * count, *([1.] * count)))
    ints(-1, -1, 0, 7, 1)
    data.append(1); ints(123); data.append(0); ints(*range(20, 28))
    ints(-1, -1, 51, 52, 0, -1, -1, -1); data.append(1)
    ints(7, -1, -1, -1, -1); data.extend((1, 0, 1)); ints(*range(10, 16))
    floats(1); ints(7, 8); floats(11); data.append(1); ints(7, -1); data.append(0)
    ints(7, 7, 7, 7, -1, -1); data.extend((0, 1)); floats(1); data.append(1)
    floats(15); ints(7, -1, 7, -1); floats(2); data.extend((1, 0)); floats(4)
    return bytes(data)


def stream(body):
    return b'DICT' + struct.pack('>IB', 9 + len(body), 0) + body + bytes(4)


def shot_fixture(component):
    data = bytearray()
    def ints(*values): data.extend(struct.pack('>' + 'i' * len(values), *values))
    def floats(count): data.extend(struct.pack('>' + 'f' * count, *([1.] * count)))
    if component == 40:
        data.append(0); floats(11); ints(-1, -1); floats(2); data.append(0); floats(1)
        ints(1, 2, -1, 3, 40, 1, 2, 1, -1); floats(1)
        ints(1, 2, 3); floats(1); data.append(0); ints(-1, -1); floats(1)
        data.extend((0, 0, 1, 0)); ints(-1); floats(1); data.append(0)
        ints(7); floats(1); ints(8, 9); data.extend((1, 0)); ints(1000)
    elif component == 38:
        ints(2000); floats(5); data.append(1); floats(2); ints(0, -1, -1, 2, 3, 4, 1000, 50)
        ints(-1, -1, -1, 2, -1, -1, 1, 2, 3); floats(1)
    elif component == 153:
        ints(7); floats(9); data.append(1); ints(8, 9, 2, 10); floats(1)
        ints(11); floats(1); data.append(0); ints(12, -1)
    return bytes(data)


class AuthorityReaders(unittest.TestCase):
    def test_remote_animation_bindings_and_all_truncations(self):
        body = struct.pack('>17f2i', *([1.] * 17), 80, 1)
        body += struct.pack('>5i2B', 100, 200, -1, 300, -1, 1, 0)
        body += struct.pack('>9i', 2, -1, -1, 1, -1, 3, -1, -1, -1)
        body += struct.pack('>6i', *([-1] * 6))
        animation = struct.pack('>2ifBi2Bi', -1, -1, 1.5, 1, -1, 0, 1, 2)
        animation += struct.pack('>2if2if3B', 1, -1, .25, 1, -1, .75, 0, 1, 0)
        for data, parser in ((body, remote_player), (animation, actor_animation)):
            reader = DictionaryStream(stream(data))
            result = parser(reader)
            reader.finish()
            if parser is remote_player:
                self.assertEqual(result, ((1.,)*17, 80,
                    (((100, 200, b'', 300, b'', True, False), (b'', b''), (b'',)),),
                    (b'', b'', b''), (b'',)*6))
            else:
                self.assertEqual(result, (b'', b'', 1.5, True, b'', (False, True),
                    (((b'', .25),), ((b'', .75),)), (False, True, False)))
            for cut in range(len(data)):
                with self.subTest(parser=parser.__name__, cut=cut), self.assertRaises(ValueError):
                    parser(DictionaryStream(stream(data[:cut])))

    def test_ragdoll_layout_and_all_truncations(self):
        body = struct.pack('>5i', 1, 40, 1, 1, 0) + struct.pack('>3i6fB', 0, -1, -1, 1, 2, 3, 4, 5, 6, 1)
        body += struct.pack('>5i3f3i', 1, 1, 2, 3, 4, 1, 2, 3, 1, 3, 5)
        reader = DictionaryStream(stream(body))
        skeletons, rules = ragdoll_settings(reader)
        reader.finish()
        self.assertEqual(skeletons, ((40, (((1, 0, 0), (b'', b''), (1., 2., 3., 4., 5., 6.), True),), (((1, 2, 3, 4), (1., 2., 3.)),)),))
        self.assertEqual(rules, ((3, 5),))
        for cut in range(len(body)):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                ragdoll_settings(DictionaryStream(stream(body[:cut])))
        component = struct.pack('>BiB17fiB', 1, 2, 0, *([1.] * 17), -1, 1)
        reader = DictionaryStream(stream(component))
        self.assertEqual(physics_ragdoll(reader), (True, 2, False, (1.,) * 17, b'', True))
        reader.finish()
        for cut in range(len(component)):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                physics_ragdoll(DictionaryStream(stream(component[:cut])))

    def test_firing_envelope_preserves_average_and_discards_unused_events(self):
        events = [(b'', t, t+1, 1, 0) for t in (-1, 0, 100, 200, 201)]
        animation = (b'clip', 1., ((b'', b'', tuple(events)),))
        state = (b'attack', 37, True, (), (animation,), ())
        self.assertAlmostEqual(firing_interval([state], {b'clip': (0, 201)}), 67.)
        self.assertNotEqual(firing_interval([state], {b'clip': (0, 201)}), 1.)
        disabled = (b'attack', 37, True, (), ((b'clip', 0., animation[2]),), ())
        self.assertEqual(firing_interval([disabled, state], {b'clip': (0, 201)}), 67.)
        with self.assertRaises(ValueError):
            firing_interval([disabled], {b'clip': (0, 201)})

    def test_player_body_clamp_and_truncation(self):
        values = (.5,.25,0.,1.,2.,3.)
        body = struct.pack('>6f', *values)
        self.assertEqual(actor_collision(DictionaryStream(stream(body))), values)
        self.assertEqual(player_body(values, values), {'radius_world_units': .5, 'height_world_units': 1.,
                         'offset_x_world_units': 1., 'offset_y_world_units': 2., 'offset_z_world_units': 3.})
        for cut in range(len(body)):
            with self.assertRaises(ValueError):
                actor_collision(DictionaryStream(stream(body[:cut])))
        with self.assertRaises(ValueError):
            player_body(values, (1., *values[1:]))

    def test_addons_require_common_verified_fields(self):
        local = {4: 10, 0x68: 20, 0x6c: 25, 0x70: .5, 0x74: 40, 0x78: 50, 0x7c: 75}
        remote = dict(local)
        remote[4] = 99  # Unresolved perks are not exported.
        self.assertEqual(weapon_addons(local, remote), {'damage_percent': 20, 'fire_speed_percent': 25,
                         'launcher_delay_multiplier': .5, 'spread_reduction_percent': 40,
                         'reload_speed_percent': 50, 'ammo_capacity_percent': 75})
        for offset in (0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c):
            changed = dict(remote)
            changed[offset] += 1
            with self.assertRaises(ValueError):
                weapon_addons(local, changed)

    def test_multiplayer_loadout_layout_and_truncation(self):
        body = struct.pack('>15f2if4i2i2if4i', *([1.] * 15), 1, 2, 1., 3, 4, 5, 6, 1, -1, 25, 40, 1., 7, 8, 9, 10)
        reader = DictionaryStream(stream(body))
        fields = multiplayer_loadout(reader)
        reader.finish()
        self.assertEqual((fields[0x68], fields[0x6c], fields[0x70], fields[0x80]), (25, 40, 1., 10))
        for cut in range(len(body)):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                multiplayer_loadout(DictionaryStream(stream(body[:cut])))

    def test_reload_timing_uses_native_event_speed_and_frame_rounding(self):
        def state(speed=1., event=4, start=550):
            return (b'Reload', 22, False, (), ((b'clip', speed, ((b'game', b'', ((b'event', start, start+1, event, 0),)),)),), ())
        self.assertEqual(reload_timing((state(.8),), {b'clip': (84900, 85566)}), {'event_ms': 550, 'animation_ms': 666, 'animation_speed': .8})
        self.assertEqual(reload_timing((state(),), {b'clip': (3833, 5166)}), {'event_ms': 550, 'animation_ms': 1333, 'animation_speed': 1.})
        for states, clips in (((), {}), ((state(), state()), {b'clip': (0, 1000)}),
                              ((state(0),), {b'clip': (0, 1000)}),
                              ((state(event=1),), {b'clip': (0, 1000)}),
                              ((state(start=1200),), {b'clip': (0, 1000)})):
            with self.assertRaises(ValueError):
                reload_timing(states, clips)

    def test_health_layout_and_every_truncation(self):
        body = struct.pack('>fBfii', 123.5, 0, 2.5, 200, 300)
        for stride in (8, 4, 8, 8, 4, 4):
            body += struct.pack('>ii', 1, -1)
            if stride == 8:
                body += struct.pack('>f', 1.25)
        body += struct.pack('>6i', *([-1]*6))
        body += struct.pack('>ii', 1, -1)
        body += struct.pack('>iii', 1, 7, 8)
        body += struct.pack('>iiiBBi', 1, -1, 9, 1, 0, -1)
        reader = DictionaryStream(stream(body))
        value = health(reader)
        reader.finish()
        self.assertEqual(value[4], 123.5)
        self.assertEqual(value[0x84], ((7, 8),))
        for cut in range(len(body)):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                health(DictionaryStream(stream(body[:cut])))

    def test_state_layout_and_every_truncation(self):
        body = bytearray()
        def ints(*values): body.extend(struct.pack('>' + 'i' * len(values), *values))
        ints(1, 1, -1, 12); body.append(0)
        ints(*range(8)); ints(1, -1)
        body.extend(struct.pack('>f', 1.5))
        ints(1, -1, -1, 1, -1, 50, 51, 1, 0)
        ints(0, 0, 0, 1, 1, 2, 3, 4, 5); body.append(1); ints(-1, -1)
        reader = DictionaryStream(stream(body))
        slots, states, initial = state_set(reader)
        reader.finish()
        self.assertEqual(slots, 1)
        self.assertEqual(states[0][1:4], (12, False, tuple(range(8))))
        self.assertEqual(states[0][4], ((b'', 1.5, ((b'', b'', ((b'', 50, 51, 1, 0),)),)),))
        self.assertEqual(states[0][5], (((1, 2, 3, 4, 5), True, b''),))
        self.assertEqual(initial, b'')
        for cut in range(len(body)):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                state_set(DictionaryStream(stream(body[:cut])))

    def test_shot_layouts_and_every_truncation(self):
        for component, parser in ((40, projectile), (38, grenade), (153, damage)):
            body = shot_fixture(component)
            reader = DictionaryStream(stream(body))
            fields = parser(reader)
            reader.finish()
            if component == 40:
                self.assertEqual(fields[0xa8], 1000)
                self.assertTrue(fields[0x86])
                self.assertFalse(fields[0x87])
            elif component == 38:
                self.assertEqual(fields[4], 2000)
                self.assertTrue(fields[0x1c])
            else:
                self.assertEqual(fields[0x38], ((10, 1.), (11, 1.)))
            for cut in range(len(body)):
                with self.subTest(component=component, cut=cut), self.assertRaises(ValueError):
                    parser(DictionaryStream(stream(body[:cut])))

    def test_weapon_layout_and_every_truncation(self):
        body = fixture()
        reader = DictionaryStream(stream(body))
        result = weapon(reader)
        reader.finish()
        self.assertEqual(result[0x1c][0][0x0c], 20)
        self.assertEqual(result[0x4c], 13)
        self.assertEqual(result[0x50], 14)
        self.assertEqual(result[0x58], 1.)
        for cut in range(len(body)):
            with self.assertRaises(ValueError):
                weapon(DictionaryStream(stream(body[:cut])))

    def test_invalid_nested_count_and_flag(self):
        for offset, replacement in ((16, b'\xff\xff\xff\xff'), (20, b'\x02')):
            body = bytearray(fixture())
            body[offset:offset+len(replacement)] = replacement
            with self.assertRaises(ValueError):
                weapon(DictionaryStream(stream(body)))

    def test_all_original_weapon_boundaries(self):
        library = ROOT / 'local/input/lib/x86/libNOVA3_neon.so'
        archives = sorted((ROOT / 'local/game/obb').glob('*.obb'))
        if os.environ.get('NOVA_REAL_TESTS') != '1' or not library.is_file() or not archives:
            self.skipTest('Original authority inputs are opt-in')
        resources = technical_resources(archives)
        index = template_index(resources['gol.bin'], ROOT / 'build/win32/tests/Release/native-world-reader-tests.exe', library)
        definitions = [weapon_template(resources['gol.bin'], index, kind) for kind, component in index if component == 71]
        self.assertEqual(len(definitions), 49)
        self.assertTrue(all(isinstance(value[0x1c], tuple) for value in definitions))
        for component, parser, count in ((40, projectile, 73), (38, grenade, 12), (153, damage, 1), (14, state_set, 416)):
            definitions = [numeric_template(resources['gol.bin'], index, kind, component, parser)
                           for kind, c in index if c == component]
            self.assertEqual(len(definitions), count)
        health_definitions = [numeric_template(resources['gol.bin'], index, kind, 17, health)
                              for kind, component in index if component == 17]
        self.assertGreater(len(health_definitions), 10)
        for kind in (1002, 1003):
            self.assertEqual(numeric_template(resources['gol.bin'], index, kind, 17, health)[4], 100.)
        body = player_body(*(numeric_template(resources['gol.bin'], index, kind, 13, actor_collision)
                             for kind in (1002, 1003)))
        self.assertAlmostEqual(body['height_world_units'], 2.1, places=6)
        loadouts = [numeric_template(resources['gol.bin'], index, kind, 58, multiplayer_loadout)
                    for kind, component in index if component == 58]
        self.assertGreaterEqual(len(loadouts), 2)
        # Unlike base health, local/remote equipment defaults differ. Do not
        # collapse them into one authoritative rule set without caller evidence.
        self.assertNotEqual(numeric_template(resources['gol.bin'], index, 1002, 58, multiplayer_loadout),
                            numeric_template(resources['gol.bin'], index, 1003, 58, multiplayer_loadout))
        common = weapon_addons(*(numeric_template(resources['gol.bin'], index, kind, 58, multiplayer_loadout)
                                 for kind in (1002, 1003)))
        self.assertEqual(common['ammo_capacity_percent'], 50)
        ragdolls = [numeric_template(resources['gol.bin'], index, kind, 140, ragdoll_settings)
                    for kind, c in index if c == 140]
        self.assertEqual(len(ragdolls), 1)
        self.assertEqual(len(ragdolls[0][0]), 5)
        selected = numeric_template(resources['gol.bin'], index, 1002, 142, physics_ragdoll)[1]
        parts = ragdolls[0][0][selected][1]
        self.assertEqual([p[0][0] for p in parts], [1, 2, 3, 4, 5, 7, 8, 10, 11, 12, 13])
        self.assertEqual([p[2][3] for p in parts], [1., 1., 3., .75, .75, .75, .75, 1., 1., 1., 1.])


if __name__ == '__main__':
    unittest.main()
