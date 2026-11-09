"""Synthetic bounds/format tests for the development technical map reader."""
import struct
import subprocess
import io
import os
import sys
import unittest
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/analysis'))
from world_data import DictionaryStream, ObjectType, collision, collision_map, collision_bounds, component, encode_collision_map, level, object_types, template_component, template_index, technical_resources
from world_data import Placement, Triangle, CollisionGroup, collision_bound_parts
from quality_profiles import records, unpack


def stream(body, strings=(b'floor',)):
    dictionary = struct.pack('>I', len(strings))
    for value in strings:
        dictionary += struct.pack('>I', len(value)) + value
    return b'DICT' + struct.pack('>IB', 9 + len(body), 0) + body + dictionary


class TechnicalDataTests(unittest.TestCase):
    def test_bounds_keep_material_groups_disabled_objects_and_negative_scale(self):
        placement = Placement(1, (10, 20, 30), (0, 0, 90), (-2, 3, 4), (0, 0))
        groups = (CollisionGroup(b'private', (
            Triangle((-1, -2, 0, 3, 4, 0, -1, 4, 2), 0),
            Triangle((10, 20, 30, 20, 30, 40, 10, 30, 40), 18))),)
        parts = collision_bound_parts(placement, groups)
        self.assertEqual(len(parts), 2)
        self.assertEqual(parts[0], (10,20,30,0,0,90,-6,-6,0,2,12,8))
        self.assertEqual(parts[1], (10,20,30,0,0,90,-40,60,120,-20,90,160))
        for bad in ([], [(0,) * 11], [(float('nan'),) * 12]):
            with self.assertRaises(ValueError):
                collision_bounds(bad, 'not-executed', 'not-executed')

    def fixture(self, coordinates=(0, 0, 0, 2, 0, 0, 0, 3, 0), material=255):
        return stream(struct.pack('>iii9fB', 1, 0, 1, *coordinates, material))

    def test_collision_preserves_material_and_local_coordinates(self):
        groups = collision(self.fixture())
        self.assertEqual(len(groups), 1)
        self.assertEqual(groups[0].node, b'floor')
        self.assertEqual(groups[0].triangles[0].vertices, (0, 0, 0, 2, 0, 0, 0, 3, 0))
        self.assertEqual(groups[0].triangles[0].material, 255)

    def test_two_distinct_groups_and_empty_group(self):
        groups = collision(stream(struct.pack('>iiiii', 2, 0, 0, 1, 0), (b'a', b'b')))
        self.assertEqual([g.node for g in groups], [b'a', b'b'])
        self.assertFalse(groups[0].triangles)

    def test_repeated_node_groups_are_not_dropped(self):
        groups = collision(stream(struct.pack('>iiiii', 2, 0, 0, 0, 0)))
        self.assertEqual([g.node for g in groups], [b'floor', b'floor'])

    def test_every_truncation_is_rejected(self):
        value = self.fixture()
        for size in range(len(value)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                collision(value[:size])

    def test_bad_boundaries_and_references(self):
        bad = [self.fixture() + b'extra', b'FAIL' + self.fixture()[4:]]
        for offset in (0, 8, 0xffffffff):
            bad.append(self.fixture()[:4] + struct.pack('>I', offset) + self.fixture()[8:])
        bad.append(self.fixture()[:8] + b'\x01' + self.fixture()[9:])
        for index in (-2, -1, 1, 0x7fffffff):
            bad.append(stream(struct.pack('>iii', 1, index, 0)))
        for count in (-1, 0x7fffffff):
            bad.append(stream(struct.pack('>i', count)))
            bad.append(stream(struct.pack('>iii', 1, 0, count)))
        bad.append(stream(struct.pack('>iii', 1, 0, 0) + b'extra'))
        bad.append(stream(struct.pack('>iii', 1, 0, 0), (b'bad\0node',)))
        for value in bad:
            with self.subTest(value=value.hex()), self.assertRaises(ValueError):
                collision(value)

    def test_nonfinite_coordinates(self):
        for value in (float('nan'), float('inf'), -float('inf')):
            for position in range(9):
                coordinates = [0] * 9
                coordinates[position] = value
                with self.assertRaises(ValueError):
                    collision(self.fixture(coordinates))

    def test_dictionary_is_private_and_not_interpreted_as_code(self):
        reader = DictionaryStream(stream(struct.pack('>ii', -1, 0), (b'synthetic script\n',)))
        self.assertEqual(reader.string(), b'')
        self.assertEqual(reader.string(), b'synthetic script\n')
        reader.finish()

    def test_spawn_is_a_record_not_a_signature_search(self):
        base = struct.pack('>i9fBB', 123, 1, 2, 3, 0, 0, 45, 1, 1, 1, 1, 0)
        payload = base + bytes((1, 0, 1)) + struct.pack('>i7i', 0, 11, 22, 33, 44, 55, 2, 77)
        value = stream(struct.pack('>HHiI', 0x4e56, 3, 1, 108) + payload + struct.pack('>i', -1),
                       (b'opaque script including a false marker \0\0\0\x6c',))
        objects = level(value, {})
        self.assertEqual(len(objects), 1)
        spawn = objects[0][2]
        self.assertEqual(spawn.placement.identifier, 123)
        self.assertEqual(spawn.placement.position, (1, 2, 3))
        self.assertEqual(spawn.placement.rotation, (0, 0, 45))
        self.assertEqual(spawn.team, 2)
        self.assertEqual((spawn.car, spawn.car_waypoint, spawn.machine_gun,
                          spawn.bike, spawn.mech, spawn.linked_spawn), (11, 22, 33, 44, 55, 77))
        self.assertFalse(hasattr(spawn, 'script'))
        for size in range(len(value)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                level(value[:size], {})

    def test_unknown_object_component_and_extra_records_fail(self):
        for body in (struct.pack('>HHiIi', 0x4e56, 3, 1, 999, -1),
                     struct.pack('>HHiIi', 0x4e56, 3, 0, 108, -1),
                     struct.pack('>HHii', 0x4e56, 2, 0, -1)):
            with self.assertRaises(ValueError):
                level(stream(body), {})
        with self.assertRaises(ValueError):
            level(stream(struct.pack('>HHiIi', 0x4e56, 3, 1, 999, -1)),
                  {999: ObjectType(((999, 0),), 0, 0)})

    def test_nested_component_counts_and_collision_links(self):
        body = bytes((0, 1, 1, 0, 0)) + struct.pack('>i', 0)
        body += struct.pack('>iiiii', 2, 1, 21, 1, 22) + struct.pack('>i', 99)
        reader = DictionaryStream(stream(body, (b'wall.bdae', b'node')))
        flags, name, links, count = component(reader, 25)
        reader.finish()
        self.assertEqual((flags, name, links, count), ((0, 1, 1, 0, 0), b'wall.bdae', ((b'node', 21), (b'node', 22)), 99))
        reader = DictionaryStream(stream(struct.pack('>iii', 1, 2, -1)))
        with self.assertRaises(ValueError):
            component(reader, 162)

    def test_multiplayer_map_order_excludes_disabled_entries_and_presentation(self):
        body = struct.pack('>ii', 4, 3)
        for filename, enabled, modes in ((0, 1, 254), (1, 0, 1), (2, 1, 118)):
            body += struct.pack('>5i3Bi', -1, filename, -1, -1, -1, 0, 0, enabled, modes)
        reader = DictionaryStream(stream(body, (b'first', b'disabled', b'last')))
        self.assertEqual(component(reader, 93), ((b'first', 254), (b'last', 118)))
        reader.finish()
        for cut in range(len(body)):
            with self.assertRaises(ValueError):
                component(DictionaryStream(stream(body[:cut], (b'first', b'disabled', b'last'))), 93)

    def test_indexed_template_header_boundaries(self):
        header = struct.pack('>HHiI', 0x474f, 3, 1, 21)
        record = struct.pack('>iihi hh', 8, 0, 1, 0, 25, 1)
        types = object_types(stream(header + record, (b'editor-name',)))
        self.assertEqual(types[8].components, ((25, 1),))
        for offset in (0, 20, 0xffffffff):
            with self.assertRaises(ValueError):
                object_types(stream(struct.pack('>HHiI', 0x474f, 3, 1, offset) + record))


class TechnicalMountTests(unittest.TestCase):
    @staticmethod
    def archive(entries):
        gla = bytearray()
        for name, payload in entries:
            encoded_name = name.encode()
            header = struct.pack('<5H3I2H', 20, 0, 0, 0, 0, zlib.crc32(payload),
                                 len(payload), len(payload), len(encoded_name), 0) + encoded_name
            key = b'levels.gla'
            gla.extend(b'SB' + struct.pack('<H', len(header)))
            gla.extend(v ^ key[i % len(key)] for i, v in enumerate(header))
            gla.extend(payload)
        output = io.BytesIO()
        with zipfile.ZipFile(output, 'w') as archive:
            archive.writestr('levels.gla', gla)
        output.seek(0)
        return output

    def test_patch_precedence_case_and_resource_allowlist(self):
        main = self.archive([('Wall.COL', b'old'), ('gol.bin', b'library'), ('image.bdae', b'not technical')])
        patch = self.archive([('wall.col', b'new'), ('map.lvc', b'level'), ('script.lua', b'not technical')])
        self.assertEqual(technical_resources([main, patch]),
                         {'wall.col': b'new', 'gol.bin': b'library', 'map.lvc': b'level'})

    def test_ambiguous_case_in_one_mount_fails(self):
        source = self.archive([('Wall.COL', b'a'), ('wall.col', b'b')])
        with self.assertRaises(ValueError):
            technical_resources([source])


class OriginalTechnicalDataTests(unittest.TestCase):
    def test_original_collision_bounds_rotation_union_and_zero_margin(self):
        root = Path(__file__).resolve().parents[1]
        library = root / 'local/input/lib/x86/libNOVA3_neon.so'
        oracle = root / 'build/win32/tests/Release/native-world-reader-tests.exe'
        if os.environ.get('NOVA_REAL_TESTS') != '1' or not library.is_file():
            self.skipTest('Original input checks are opt-in')
        part = (10,20,30,0,0,0,-1,-2,0,3,4,2)
        self.assertEqual(collision_bounds([part], oracle, library), (9,18,30,13,24,32))
        rotated = part[:5] + (90,) + part[6:]
        actual = collision_bounds([rotated], oracle, library)
        for got, want in zip(actual, (6,19,30,12,23,32)):
            self.assertAlmostEqual(got, want, places=5)
        combined = collision_bounds([part, rotated], oracle, library)
        for got, want in zip(combined, (6,18,30,13,24,32)):
            self.assertAlmostEqual(got, want, places=5)
        env = dict(os.environ, NOVA_REAL_TESTS='1')
        valid = struct.pack('<I12f', 1, *part)
        for bad in (b'', struct.pack('<I', 0), valid[:-1], valid+b'x'):
            process = subprocess.run([str(oracle), str(library), '--collision-bounds'],
                                     input=bad, capture_output=True, timeout=30, env=env)
            self.assertNotEqual(process.returncode, 0)

    def test_original_archives_and_template_boundaries(self):
        root = Path(__file__).resolve().parents[1]
        obb = root / 'local/game/obb/main.1050.com.gameloft.android.ANMP.GloftN3HM.obb'
        library = root / 'local/input/lib/x86/libNOVA3_neon.so'
        oracle = root / 'build/win32/tests/Release/native-world-reader-tests.exe'
        if os.environ.get('NOVA_REAL_TESTS') != '1' or not obb.is_file() or not library.is_file():
            self.skipTest('Original input checks are opt-in')
        with zipfile.ZipFile(obb) as archive:
            source = archive.read('levels.gla')
        # The compressed archives differ, but the mounted technical members
        # are identical in these supported inputs. Resolve reference spelling
        # case-insensitively and honor patch precedence rather than ignoring it.
        mounted = technical_resources(sorted(obb.parent.glob('*.obb')))
        levels = {}
        collision_files = 0
        for name, method, payload, size, crc in records(source, b'levels.gla'):
            if name.endswith('.col'):
                original = unpack(method, payload, size, crc)
                self.assertEqual(mounted[name.casefold()], original)
                collision(original)
                collision_files += 1
            elif name == 'gol.bin' or name.startswith('DM_MAP_') and name.endswith('.lvc'):
                levels[name] = unpack(method, payload, size, crc)
                self.assertEqual(mounted[name.casefold()], levels[name])
        self.assertEqual(collision_files, 793)
        types = object_types(levels['gol.bin'])
        parsed = [level(value, types) for name, value in levels.items() if name.endswith('.lvc')]
        self.assertEqual(len(parsed), 7)
        self.assertEqual(sum(kind == 108 for objects in parsed for _, kind, _ in objects), 259)
        self.assertTrue(oracle.is_file(), 'Build native-world-reader-tests before original checks')
        index = template_index(levels['gol.bin'], oracle, library)
        self.assertEqual(len(index), 4428)
        maps = template_component(levels['gol.bin'], index, 1040, 93)
        self.assertEqual([modes for _, modes in maps], [254, 254, 246, 30, 118, 158])
        self.assertEqual(len(maps), 6)
        self.assertTrue(all(name.decode().casefold() + '.lvc' in mounted for name, _ in maps))
        geometry = [collision_map(mounted, index, name) for name, _ in maps]
        self.assertEqual([sum(len(c['triangles']) for c in m['colliders']) for m in geometry],
                         [23029, 44598, 97378, 44073, 100517, 25179])
        self.assertEqual([len(m['spawns']) for m in geometry], [47, 32, 56, 29, 36, 23])
        for m in geometry:
            for collider in m['colliders']:
                self.assertEqual(set(collider), {'id', 'type', 'position', 'rotation', 'scale',
                                                'enabled', 'static', 'transparent', 'triangles'})
            for spawn in m['spawns']:
                self.assertEqual(set(spawn), {'id', 'position', 'rotation', 'scale', 'enabled',
                                             'level_start', 'team', 'car', 'car_waypoint',
                                             'machine_gun', 'bike', 'mech', 'linked_spawn'})
        self.assertTrue(any(not c['enabled'] and c['triangles'] for m in geometry for c in m['colliders']))
        self.assertTrue(any(not c['static'] and c['triangles'] for m in geometry for c in m['colliders']))
        for map_id, (metadata, (_, modes)) in enumerate(zip(geometry, maps)):
            metadata['position_bounds'] = collision_bounds(metadata['bounds_parts'], oracle, library)
            section = encode_collision_map(metadata, map_id, modes)
            self.assertEqual(section[:8], b'NV3C\x03' + struct.pack('>BH', map_id, modes))
            # Optional independent Go consumer, supplied only by development
            # checks. Bytes go over stdin; no map fixture is retained on disk.
            if consumer := os.environ.get('NOVA_WORLD_TEST_BIN'):
                result = subprocess.run([consumer, '-test.run=^TestCompiledCollisionMap$'], input=section,
                                        capture_output=True, timeout=45,
                                        env=dict(os.environ, NOVA_MAP_STDIN='1'))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # Missing an ordinary collision file is not a reason to ship a hole.
        incomplete = {key: value for key, value in mounted.items() if not key.endswith('.col')}
        with self.assertRaisesRegex(ValueError, 'Missing collision resource'):
            collision_map(incomplete, index, maps[0][0])
        # Compare all mesh/collision template boundaries with their original
        # readers. This includes shared templates used by placed objects.
        for kind, component_id in index:
            if component_id in (1, 25):
                template_component(levels['gol.bin'], index, kind, component_id)


if __name__ == '__main__':
    unittest.main()
