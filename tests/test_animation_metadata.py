"""Synthetic timing metadata; no animation samples or original names."""
import io
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from animation_metadata import clips, decode_name, pak_index, weapon_clips


def resource():
    data = bytearray(180)
    data[:8] = b'BRES\xfe\xff\0\0'
    for offset, value in ((8, 60), (12, len(data)), (16, 3), (24, 60),
                          (32, 72), (60, 32), (64, 132), (68, 136),
                          (128, 1), (132, 136), (136, 152),
                          (140, 150), (144, 450), (148, 4)):
        struct.pack_into('<I', data, offset, value)
    data[152:156] = b'test'
    return data


class AnimationMetadata(unittest.TestCase):
    def test_bounded_clip_metadata(self):
        data = resource()
        # Nonzero archive offset must not affect resource-relative pointers.
        self.assertEqual(clips(io.BytesIO(b'prefix' + data), 6, len(data)),
                         {b'test': (150, 450)})
        # Track/sample tail is never read: this reader consumes metadata only.
        self.assertEqual(clips(io.BytesIO(data[:156]), 0, len(data)),
                         {b'test': (150, 450)})
        for cut in range(156):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                clips(io.BytesIO(data[:cut]), 0, len(data))

    def test_invalid_clip_metadata(self):
        for offset, value in ((8, 61), (12, 181), (16, 100), (24, 178),
                              (32, 180), (60, 0), (64, 0), (68, 0),
                              (128, 20001), (132, 179), (136, 3),
                              (140, 451), (144, 0), (148, 257)):
            data = resource()
            struct.pack_into('<I', data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                clips(io.BytesIO(data), 0, len(data))

    def test_archive_bounds(self):
        # This deliberately synthetic obfuscated name is decoded independently
        # of the resource. Its numeric bytes are not original archive content.
        encoded = b'abcd'
        data = (struct.pack('>4I', 0, 37, 32, 1) +
                struct.pack('>4I', 37, 4, 0, 0) + encoded + b'\0data')
        self.assertEqual(pak_index(io.BytesIO(data), len(data)),
                         {decode_name(encoded).lower(): (37, 4, 0)})
        for cut in range(37):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                pak_index(io.BytesIO(data[:cut]), len(data))
        for offset, value in ((4, 42), (8, 31), (12, 20001),
                              (16, 36), (20, 5), (24, 4)):
            broken = bytearray(data)
            struct.pack_into('>I', broken, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                pak_index(io.BytesIO(broken), len(data))
        broken = bytearray(data)
        broken[36] = 1
        with self.assertRaises(ValueError):
            pak_index(io.BytesIO(broken), len(data))

    def test_original_quality_timings(self):
        archives = sorted((ROOT / 'local/game/obb').glob('*.obb'))
        if os.environ.get('NOVA_REAL_TESTS') != '1' or not archives:
            self.skipTest('Original timing inputs are opt-in')
        high = weapon_clips(archives, b'hands_weapons_anim.bdae')
        low = weapon_clips(archives, b'hands_weapons_anim_low.bdae')
        self.assertEqual(len(high), 531)
        self.assertEqual(high, low)


if __name__ == '__main__':
    unittest.main()
