"""Read weapon timing metadata in place; never extract animation samples.

Reuses the custom-Pak/BRES layouts established by the earlier controller-binding
research. This development module returns clip times only. It does not write
names, resource headers, animation tracks or samples into the server package.
"""
import struct
import zipfile


def decode_name(data):
    # Existing archive-name obfuscation, independently implemented in the port's
    # prior resource research. Each name restarts the generator.
    result, state = bytearray(), 0x3857a
    def random():
        nonlocal state
        state = (state * 0x19660d + 0x3c6ef35f) & 0xffffffff
        return state
    for start in range(0, len(data), 4):
        chunk = data[start:start+4]
        bits = len(chunk) * 8
        mask = (1 << bits) - 1
        direction, rotation = (random() >> 16) & 1, (random() >> 16) % bits
        value = int.from_bytes(chunk, 'little')
        if direction:
            value = ((value >> rotation) | (value << (bits-rotation))) & mask
        else:
            value = ((value << rotation) | (value >> (bits-rotation))) & mask
        key = 0
        for _ in chunk:
            key = (key << 8) | ((random() >> 16) & 255)
        result.extend((value ^ key).to_bytes(len(chunk), 'little'))
    return bytes(result)


def pak_index(stream, size):
    header = stream.read(16)
    if len(header) != 16:
        raise ValueError('Truncated timing archive')
    _, end, start, count = struct.unpack('>4I', header)
    if not 0 < count <= 20000 or start != 16+16*count or not start <= end <= min(size, 1 << 20):
        raise ValueError('Unsupported timing archive index')
    table, names = stream.read(start-16), stream.read(end-start)
    if len(table) != start-16 or len(names) != end-start:
        raise ValueError('Truncated timing archive index')
    entries = list(struct.iter_unpack('>4I', table))
    result = {}
    for i, (offset, length, name_start, flags) in enumerate(entries):
        name_end = entries[i+1][2]-1 if i+1 < count else len(names)-1
        if not 0 <= name_start < name_end < len(names) or names[name_end] != 0 or not end <= offset <= offset+length <= size:
            raise ValueError('Invalid timing archive entry')
        name = decode_name(names[name_start:name_end]).lower()
        if name in result:
            raise ValueError('Ambiguous timing archive entry')
        result[name] = offset, length, flags
    return result


def clips(stream, offset, size):
    def take(position, length):
        if not 0 <= position <= position+length <= size:
            raise ValueError('Timing metadata exceeds resource')
        stream.seek(offset+position)
        value = stream.read(length)
        if len(value) != length:
            raise ValueError('Truncated timing metadata')
        return value
    def word(position):
        return struct.unpack('<I', take(position, 4))[0]
    if take(0, 8) != b'BRES\xfe\xff\0\0' or word(8) != 60 or word(12) != size:
        raise ValueError('Unsupported timing resource')
    relocation_count, relocation_start = word(16), word(24)
    if relocation_count > size//4:
        raise ValueError('Invalid timing relocation count')
    relocations = set(struct.unpack(f'<{relocation_count}I', take(relocation_start, relocation_count*4)))
    root = word(32)
    if 32 not in relocations or root+60 not in relocations:
        raise ValueError('Missing timing-library relocation')
    count, table = struct.unpack('<II', take(root+56, 8))
    if not 0 < count <= 20000:
        raise ValueError('Invalid timing clip count')
    entries = take(table, count*12)
    result = {}
    for i, (name_pointer, start, end) in enumerate(struct.iter_unpack('<Iii', entries)):
        if table+12*i not in relocations or start < 0 or end < start:
            raise ValueError('Invalid timing clip')
        length = word(name_pointer-4)
        if not 0 < length <= 256:
            raise ValueError('Invalid timing clip name')
        name = take(name_pointer, length)
        if name in result:
            raise ValueError('Ambiguous timing clip')
        result[name] = start, end
    return result


def weapon_clips(archives, name):
    selected = None
    for path in archives:
        with zipfile.ZipFile(path) as archive:
            if 'weapons_stream.gla' in archive.namelist():
                selected = path
    if selected is None:
        raise ValueError('Missing weapon timing archive')
    for pack in ('weapons_stream.gla', 'actors_stream.gla'):
        for path in reversed(archives):
            with zipfile.ZipFile(path) as archive:
                if pack not in archive.namelist(): continue
                with archive.open(pack) as stream:
                    index = pak_index(stream, archive.getinfo(pack).file_size)
                    if name.lower() not in index: continue
                    offset, size, flags = index[name.lower()]
                    if flags != 0: raise ValueError('Unsupported encoded timing metadata')
                    return clips(stream, offset, size)
    raise ValueError('Missing weapon timing resource')
