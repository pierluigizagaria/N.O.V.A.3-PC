"""Synthetic coverage for the local scrambled-profile reader."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location(
    "profiles", Path(__file__).resolve().parents[1] / "tools/analysis/quality_profiles.py")
profiles = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profiles)


def record(name, value, compressed=False):
    key = b"options.gla"
    payload = value
    method = 0
    if compressed:
        compressor = zlib.compressobj(wbits=-15)
        payload = compressor.compress(value) + compressor.flush()
        method = 8
    name = name.encode()
    header = struct.pack("<5H3I2H", 20, 0, method, 0, 0, zlib.crc32(value),
                         len(payload), len(value), len(name), 0) + name
    encoded = bytes(v ^ key[i % len(key)] for i, v in enumerate(header))
    return b"SB" + struct.pack("<H", len(encoded)) + encoded + payload


class ProfileTests(unittest.TestCase):
    def test_stored_deflate_and_directory_footer(self):
        data = record("GPU_5.xml", b"<settings/>") + record("CPU_M.xml", b"cpu", True) + b"SDIR"
        records = list(profiles.records(data))
        self.assertEqual([r[0] for r in records], ["GPU_5.xml", "CPU_M.xml"])
        self.assertEqual([profiles.unpack(*r[1:]) for r in records], [b"<settings/>", b"cpu"])

    def test_crc_and_truncation(self):
        data = record("MEM_512.xml", b"memory")
        name, method, payload, size, crc = next(profiles.records(data))
        with self.assertRaises(ValueError):
            profiles.unpack(method, payload, size, crc ^ 1)
        with self.assertRaises(ValueError):
            list(profiles.records(data[:-1]))
        with self.assertRaises(ValueError):
            list(profiles.records(b"XX"))

    def test_flatten_preserves_attributes_and_repeated_children(self):
        value = profiles.ET.fromstring('<x value="high"><a>one</a><a>two</a></x>')
        self.assertEqual(profiles.flatten(value),
                         {"/x/@value": "high", "/x[0]/a": "one", "/x[1]/a": "two"})


if __name__ == "__main__":
    unittest.main()
