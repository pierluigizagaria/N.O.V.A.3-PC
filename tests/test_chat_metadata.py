import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from chat_metadata import string_names, global_messages
from world_data import technical_resources, template_index


class ChatMetadata(unittest.TestCase):
    def test_index_bounds_and_duplicates(self):
        data = struct.pack('<H', 2) + b'\x02\0a\0\x02\0b\0'
        self.assertEqual(string_names(data), [b'a', b'b'])
        for broken in [data[:i] for i in range(len(data))] + [data+b'\0', data[:-2]+b'a\0', data[:-1]+b'x']:
            with self.assertRaises(ValueError):
                string_names(broken)

    def test_original_template_boundary(self):
        if os.environ.get('NOVA_REAL_TESTS') != '1' or not (ROOT/'local/input/lib/x86/libNOVA3_neon.so').exists():
            self.skipTest('Original metadata is opt-in')
        gol = technical_resources(sorted((ROOT/'local/game/obb').glob('*.obb')))['gol.bin']
        index = template_index(gol, ROOT/'build/win32/tests/Release/native-world-reader-tests.exe',
                               ROOT/'local/input/lib/x86/libNOVA3_neon.so')
        # The all-tab only needs the final group. Other groups are consumed for
        # boundary verification, without exporting names or audio references.
        from world_data import DictionaryStream
        reader = DictionaryStream(gol); reader.pos = index[6010,119][0]-9
        names = []
        for _ in range(6):
            for _ in range(reader.count(8)):
                names.append(b'STR_'+reader.string()); reader.string()
        self.assertEqual(global_messages(gol,index,names), list(range(len(names)-5,len(names))))


if __name__ == '__main__':
    unittest.main()
