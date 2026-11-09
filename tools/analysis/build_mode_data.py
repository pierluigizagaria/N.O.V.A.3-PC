"""Export only approved numeric multiplayer objective rules and placements.

Original dictionaries, labels, resources and code remain private. This separate
versioned resource does not regenerate concurrent combat/collision authority.
"""
import hashlib
import json
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile

from authority_data import numeric_template
from world_data import technical_resources, object_types, template_index, template_component, level


def build(root):
    library = root / 'local/input/lib/x86/libNOVA3_neon.so'
    identity = hashlib.sha256(library.read_bytes()).hexdigest()
    if identity != 'a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e':
        raise ValueError('Unsupported original library')
    resources = technical_resources(sorted((root / 'local/game/obb').glob('*.obb')))
    gol = resources['gol.bin']
    index = template_index(gol, root / 'build/win32/tests/Release/native-world-reader-tests.exe', library)
    types = object_types(gol)
    with library.open('rb') as stream:
        elf = ELFFile(stream)
        def scalar(address):
            stream.seek(next(elf.address_offsets(address)))
            return struct.unpack('<f', stream.read(4))[0]
        stream.seek(next(elf.address_offsets(0xe8d930)))
        point_ready_ms = struct.unpack('<i', stream.read(4))[0]
        rules = {'freeze_rescue_distance_squared': scalar(0xe7f374),
	             'overtime_minutes': 3,
	             'point_ready_ms': point_ready_ms,
	             'spawn_teammate_clearance': scalar(0xe7f43c),
	             'spawn_objective_enemy_clearance': scalar(0xe77b50),
	             'spawn_fallback_count': 12,
                 'freeze_rescue_ms': 3000, 'freeze_round_settle_ms': 500,
                 'point_hold_ms': 20000, 'point_maximum': scalar(0xe7db58),
                 'point_snapshot_scale': scalar(0xe7f468),
                 'flag_home_tolerance': scalar(0xe7741c),
                 'flag_return_tolerance': scalar(0xe7db28)}
    gains = [numeric_template(gol, index, kind, c, lambda r: r.unpack('35i'))
             for kind, c in index if c == 117]
    if len(gains) != 1 or any(x < 0 for x in gains[0]):
        raise ValueError('Ambiguous objective scoring')
    rewards = {name: gains[0][offset // 4 - 1] for name, offset in {
        'flag_pick': 0x34, 'flag_return': 0x38, 'flag_capture': 0x3c,
        'point_capture': 0x40, 'point_enemy_capture': 0x44, 'point_score': 0x48,
        'freeze_first': 0x4c, 'freeze_streak': 0x54,
        'unfreeze_first': 0x64, 'unfreeze_streak': 0x68}.items()}
    maps = []
    for map_id, (filename, _) in enumerate(template_component(gol, index, 1040, 93)):
        objects = {'flags': [], 'bases': [], 'points': []}
        zones = []
        for offset, kind, value in level(resources[filename.decode('ascii').casefold()+'.lvc'], types):
            zones = [(end, zone) for end, zone in zones if offset < end]
            if kind == 62:
                base, _, end = value
                zones.append((end, base.identifier))
                continue
            if kind not in types:
                continue
            components = dict(types[kind].components)
            for component, collection in ((102, 'flags'), (103, 'bases'), (155, 'points')):
                if component not in components:
                    continue
                base = value[0]
                if not zones or base.scale != (1., 1., 1.):
                    raise ValueError('Objective needs a resolved zone and unit scale')
                tuning = template_component(gol, index, kind, component) if components[component] else value[component]
                row = {'object': base.identifier, 'zone': zones[-1][1],
                       'position': base.position, 'enabled': bool(base.flags[0])}
                if component == 155:
                    radius, team, rate, threshold = tuning[0]
                    row.update(radius=radius, team=team, rate=rate, threshold=threshold)
                else:
                    row.update(radius=tuning[0], team=tuning[1])
                objects[collection].append(row)
        maps.append({'map': map_id, **objects})
    return {'schema_version': 1, 'library_sha256': identity,
            'world_sha256': hashlib.sha256(gol).hexdigest(),
            'rules': rules, 'rewards': rewards, 'maps': maps}


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    result = build(root)
    (root / 'server/internal/match/mode_data.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Exported numeric multiplayer objectives for', len(result['maps']), 'maps')
