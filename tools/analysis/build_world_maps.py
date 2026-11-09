"""Build the approved collision/spawn data for the independent server.

Development only: read original inputs in place, use the checked native reader
for template boundaries, and emit one canonical allowlisted technical resource.
Normal server builds and installations do not execute this tool.
"""
import gzip
import hashlib
from pathlib import Path
import struct

from world_data import (collision_bounds, collision_map, encode_collision_map, technical_resources,
                        template_component, template_index)


def build(root):
    library = root / 'local/input/lib/x86/libNOVA3_neon.so'
    if hashlib.sha256(library.read_bytes()).hexdigest() != 'a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e':
        raise ValueError('Unsupported original library')
    resources = technical_resources(sorted((root / 'local/game/obb').glob('*.obb')))
    gol = resources['gol.bin']
    if hashlib.sha256(gol).hexdigest() != '26527276476c4e18b63b3b12f33b20925a63b30a5359d4a2d19a1e3cda628ff3':
        raise ValueError('Unsupported world library')
    oracle = root / 'build/win32/tests/Release/native-world-reader-tests.exe'
    index = template_index(gol, oracle, library)
    maps = template_component(gol, index, 1040, 93)
    if len(maps) != 6:
        raise ValueError('Unexpected enabled map catalogue')
    bundle = bytearray(b'NV3W\x01' + bytes([len(maps)]))
    for map_id, (filename, modes) in enumerate(maps):
        metadata = collision_map(resources, index, filename)
        metadata['position_bounds'] = collision_bounds(metadata['bounds_parts'], oracle, library)
        section = encode_collision_map(metadata, map_id, modes)
        bundle += struct.pack('>I', len(section)) + section
        print(f'Map {map_id}: {len(metadata["colliders"])} collision objects, {len(metadata["spawns"])} spawns')
    # No source names/dictionaries/scripts/render data enter either encoder.
    payload = gzip.compress(bundle, compresslevel=9, mtime=0)
    output = root / 'server/internal/world/maps.bin.gz'
    output.write_bytes(payload)
    print(f'{output.relative_to(root)}: {len(payload)} bytes, SHA256 {hashlib.sha256(payload).hexdigest()}')


if __name__ == '__main__':
    build(Path(__file__).resolve().parents[2])
