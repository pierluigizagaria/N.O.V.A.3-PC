"""Extract and compare GPU/CPU/MEM XMLs from locally supplied options.gla archives.

The observed SB records contain a 26-byte ZIP local-header tail and name,
XOR-scrambled with the archive basename. Payloads are stored or raw deflate.
All extracted data and machine-readable reports must remain under local/.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]

def records(data, key=b'options.gla'):
    offset = 0
    while offset < len(data):
        if data[offset:offset + 4] == b'SDIR':
            return  # The runtime also stops its sequential scan at the directory footer.
        if data[offset:offset + 2] != b'SB':
            raise ValueError(f'Unexpected record at {offset:#x}')
        length = struct.unpack_from('<H', data, offset + 2)[0]
        if length < 26 or offset + 4 + length > len(data):
            raise ValueError('Truncated scrambled header')
        encoded = data[offset + 4:offset + 4 + length]
        header = bytes(v ^ key[i % len(key)] for i, v in enumerate(encoded))
        version, flags, method, clock, date, crc, size, unpacked, namesize, extrasize = struct.unpack_from('<5H3I2H', header)
        name = header[26:26 + namesize].decode('utf-8')
        if flags & 8:
            crc, size, unpacked = struct.unpack_from('<3I', header, 26 + namesize + extrasize)
        start = offset + 4 + length
        end = start + size
        if end > len(data):
            raise ValueError(f'Truncated payload: {name}')
        yield name, method, data[start:end], unpacked, crc
        offset = end

def unpack(method, payload, size, crc):
    value = payload if method == 0 else zlib.decompress(payload, -15) if method == 8 else None
    if value is None or len(value) != size or zlib.crc32(value) != crc:
        raise ValueError('Unsupported method or mismatched size/CRC')
    return value

def flatten(element, prefix=''):
    path = prefix + '/' + element.tag
    values = {path + '/@' + key: value for key, value in element.attrib.items()}
    if element.text and element.text.strip():
        values[path] = element.text.strip()
    for index, child in enumerate(element):
        values.update(flatten(child, path + f'[{index}]'))
    return values

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--obb-dir', type=Path, default=ROOT / 'local/game/obb')
    p.add_argument('--output', type=Path, default=ROOT / 'local/analysis/t04-profiles')
    a = p.parse_args()
    output = a.output.resolve()
    if not output.is_relative_to((ROOT / 'local').resolve()):
        p.error('Extracted game data must stay below local/')
    report = {}
    for obb in sorted(a.obb_dir.glob('*.obb')):
        with zipfile.ZipFile(obb) as archive:
            data = archive.read('options.gla')
        profiles = {}
        for name, method, payload, size, crc in records(data):
            if not re.fullmatch(r'(GPU|CPU|MEM)_[^/\\]+\.xml', Path(name).name, re.I):
                continue
            xml = unpack(method, payload, size, crc)
            dest = output / obb.stem / Path(name).name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(xml)
            # Preserve XML locally. The report is also local; repository docs summarize in our own words.
            profiles[name] = flatten(ET.fromstring(xml))
        report[obb.name] = profiles
        print(f'{obb.name}: {len(profiles)} profiles: ' + ', '.join(sorted(profiles)))
    (output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
