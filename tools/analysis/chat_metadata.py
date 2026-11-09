"""Compile numeric preset-chat IDs only; no localized text, names or audio.

Original inputs are read in place during development. The server embeds the
small generated allowlist and never loads game archives or localization files.
"""
import io
import json
from pathlib import Path
import struct
import zipfile

from animation_metadata import decode_name, pak_index
from world_data import DictionaryStream, technical_resources, template_index


def string_names(data):
    if len(data) < 2 or len(data) > 1 << 20:
        raise ValueError('Invalid localization index size')
    count, = struct.unpack_from('<H', data)
    if not 0 < count <= 20000:
        raise ValueError('Invalid localization index count')
    names, cursor = [], 2
    for _ in range(count):
        if cursor + 2 > len(data):
            raise ValueError('Truncated localization index')
        size, = struct.unpack_from('<H', data, cursor)
        cursor += 2
        if not 1 <= size <= 256 or cursor + size > len(data):
            raise ValueError('Invalid localization name size')
        value = data[cursor:cursor+size]
        if value[-1] != 0 or b'\0' in value[:-1] or value[:-1] in names:
            raise ValueError('Invalid or duplicate localization name')
        names.append(value[:-1])
        cursor += size
    if cursor != len(data):
        raise ValueError('Trailing localization index data')
    return names


def chat_messages(gol, index, names):
    # Native CComponentMPChatInfo119 has six vectors of string pairs. The first
    # five are mode-specific team commands; the final vector is global chat.
    start, end = index[6010, 119]
    reader = DictionaryStream(gol)
    reader.pos = start - 9
    groups = []
    for _ in range(6):
        messages = []
        for _ in range(reader.count(8)):
            key = reader.string()
            reader.string()  # Audio reference never leaves development input.
            messages.append(names.index(b'STR_' + key))
        groups.append(messages)
    if reader.pos + 9 != end or not 1 <= len(messages) <= 64:
        raise ValueError('Invalid native chat template boundary')
    # CreateAllTab uses the first ID plus row index, so verify contiguity rather
    # than assuming that each resource name is independently used by the UI.
    if messages != list(range(messages[0], messages[0] + len(messages))):
        raise ValueError('Unsupported global chat ID ordering')
    return groups


def global_messages(gol, index, names):
    return chat_messages(gol, index, names)[-1]


def build(root):
    archives = sorted((root / 'local/game/obb').glob('*.obb'))
    gol = technical_resources(archives)['gol.bin']
    index = template_index(gol, root / 'build/win32/tests/Release/native-world-reader-tests.exe',
                           root / 'local/input/lib/x86/libNOVA3_neon.so')
    names = None
    for path in archives:
        with zipfile.ZipFile(path) as archive:
            if 'strings.gla' not in archive.namelist():
                continue
            data = archive.read('strings.gla')
        entries = pak_index(io.BytesIO(data), len(data))
        if b'main_map.bin' not in entries:
            continue
        offset, size, flags = entries[b'main_map.bin']
        if flags != 1:
            raise ValueError('Unsupported localization index encoding')
        names = string_names(decode_name(data[offset:offset+size]))
    if names is None:
        raise ValueError('Missing localization identity table')
    groups = chat_messages(gol, index, names)
    result = {'schema_version': 1, 'global_messages': groups[-1],
              'team_messages': {mode: groups[i] for i, mode in enumerate((1, 2, 3, 4, 6))}}
    output = root / 'server/internal/protocol/game/chat_ids.json'
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'{output.relative_to(root)}: {len(result["global_messages"])} numeric IDs')


if __name__ == '__main__':
    build(Path(__file__).resolve().parents[2])
