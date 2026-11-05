"""Convert a PNG into a Windows .ico for the executables' icon resource.

Used at CMake configure time with the user's local original icon
(local/launcher/icon.png, imported by the launcher or by tools/release.py).
The PNG and the generated .ico stay outside the repository.

With Pillow installed the icon gets 16-256 px frames for crisp small icons;
without it a single PNG frame is embedded, which Windows Vista+ scales.
"""
import io
import struct
import sys
from pathlib import Path

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def png_frames(source):
    data = source.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'make_icon: {source} is not a PNG file')
    try:
        from PIL import Image
    except ImportError:
        width, height = struct.unpack('>II', data[16:24])
        return [(width, height, data)]
    image = Image.open(io.BytesIO(data)).convert('RGBA')
    frames = []
    for size in SIZES:
        out = io.BytesIO()
        image.resize((size, size), Image.LANCZOS).save(out, 'PNG')
        frames.append((size, size, out.getvalue()))
    return frames


def write_icon(frames, target):
    header = struct.pack('<HHH', 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    directory, images = b'', b''
    for width, height, png in frames:
        # Width/height 0 means 256 in the ICONDIRENTRY byte fields.
        directory += struct.pack('<BBBBHHII', width % 256, height % 256, 0, 0, 1, 32, len(png), offset)
        images += png
        offset += len(png)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(header + directory + images)


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: make_icon.py <input.png> <output.ico>')
    write_icon(png_frames(Path(sys.argv[1])), Path(sys.argv[2]))


if __name__ == '__main__':
    main()
