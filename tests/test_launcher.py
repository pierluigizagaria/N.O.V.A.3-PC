"""Synthetic first-run/import and process tests; never starts the real game."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

PROBE, CHILD, WORK = map(Path, sys.argv[1:4])
sys.argv[1:] = []
WORK.mkdir(parents=True, exist_ok=True)
LIBRARY = 'lib/x86/libNOVA3_neon.so'
MAIN = 'main.1050.com.gameloft.android.ANMP.GloftN3HM.obb'
PATCH = 'patch.1070.com.gameloft.android.ANMP.GloftN3HM.obb'
PACKAGE = 'com.gameloft.android.ANMP.GloftN3HM'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive(path, entries, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, 'w', method) as out:
        for name, content in entries:
            out.writestr(name, content)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=WORK)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / 'portable space \u6e38\u620f'
        self.root.mkdir()
        self.sources = self.base / 'original sources'
        self.sources.mkdir()
        self.apk = self.sources / 'game.apk'
        self.main = self.sources / MAIN
        self.patch = self.sources / PATCH
        self.spec = self.base / 'fixture-spec.txt'
        # A synthetic ELF32/i386 ET_DYN header. No original game bytes or execution.
        self.library = b'\x7fELF\x01\x01\x01' + bytes(9) + struct.pack('<HHI', 3, 3, 1) + bytes(28) + b'fixture' * 30000
        archive(self.apk, [(LIBRARY, self.library)])
        archive(self.main, [('strings.gla', b'old strings'), ('z1.etc', b'MAIN-DATA' * 100000),
                            ('unused.bin', b'not extracted')])
        archive(self.patch, [('strings.gla', b'patch wins'), ('patch-only.gla', b'not extracted')])
        self.write_spec()

    def write_spec(self):
        self.spec.write_text('\n'.join([
            digest(self.apk), LIBRARY, hashlib.sha256(self.library).hexdigest(), str(len(self.library)),
            MAIN, digest(self.main), str(self.main.stat().st_size),
            PATCH, digest(self.patch), str(self.patch.stat().st_size),
            'strings.gla', 'z1.etc']), encoding='ascii')

    def run_probe(self, mode='prepare', apk=None, data=None, expect=0):
        result = subprocess.run([str(PROBE), mode, str(self.root), str(self.spec),
                                 str(self.apk if apk is None else apk),
                                 str(self.sources if data is None else data)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, expect, result.stdout + result.stderr)
        return result

    @property
    def resources(self):
        return self.root / f'local/profiles/player/sdcard/Android/data/{PACKAGE}/files'

    @property
    def receipt(self):
        return self.root / 'local/launcher/ready.txt'

    def test_prepare_patch_precedence_preservation_and_direct_ready(self):
        self.resources.mkdir(parents=True)
        save = self.resources / 'savegame.dat'
        save.write_bytes(b'player-owned')
        config = self.root / 'nova3pc.conf'
        config.write_text('world_fov = 90\n')
        original = {p: (digest(p), p.stat().st_mtime_ns) for p in self.sources.iterdir()}
        self.run_probe()
        self.assertEqual((self.resources / 'strings.gla').read_bytes(), b'patch wins')
        self.assertFalse((self.resources / 'patch-only.gla').exists())
        self.assertFalse((self.resources / 'unused.bin').exists())
        self.assertEqual(save.read_bytes(), b'player-owned')
        self.assertEqual(config.read_text(), 'world_fov = 90\n')
        self.assertEqual(original, {p: (digest(p), p.stat().st_mtime_ns) for p in original})
        times = {p: p.stat().st_mtime_ns for p in self.resources.iterdir()}
        self.run_probe('ready', apk='-', data='-')
        self.run_probe(apk='-', data='-')
        self.assertEqual(times, {p: p.stat().st_mtime_ns for p in times})
        # Sources are not required after preparation.
        self.apk.rename(self.apk.with_suffix('.moved'))
        self.run_probe('ready', apk='-', data='-')

    def test_missing_inputs_are_requested_separately(self):
        self.run_probe(apk='-', data='-', expect=12)
        separate = self.base / 'apk-only'
        separate.mkdir()
        apk = separate / 'game.apk'
        apk.write_bytes(self.apk.read_bytes())
        self.run_probe(apk=apk, data='-', expect=13)
        self.assertFalse(self.receipt.exists())

    def test_auto_detect_data_next_to_apk(self):
        self.run_probe(data='-')
        self.run_probe('ready')

    def test_auto_detect_nested_data_zip_and_ignore_traversal(self):
        bundle = self.sources / 'my-data.ZIP'
        archive(bundle, [('folder/' + MAIN, self.main.read_bytes()),
                         ('folder/' + PATCH, self.patch.read_bytes()), ('../escaped.txt', b'bad')])
        self.main.unlink()
        self.patch.unlink()
        self.run_probe(data='-')
        self.assertFalse((self.root / 'escaped.txt').exists())
        self.assertFalse((self.base / 'escaped.txt').exists())

    def test_wrong_apk_preserves_existing_library(self):
        target = self.root / 'local/input' / LIBRARY
        target.parent.mkdir(parents=True)
        target.write_bytes(b'existing library')
        self.apk.write_bytes(self.apk.read_bytes() + b'changed')
        self.run_probe(expect=1)
        self.assertEqual(target.read_bytes(), b'existing library')
        self.assertFalse(self.receipt.exists())

    def test_explicit_wrong_apk_rejected_with_valid_cached_library(self):
        self.run_probe()
        target = self.root / 'local/input' / LIBRARY
        before = (digest(target), target.stat().st_mtime_ns)
        original = self.apk.read_bytes()
        self.apk.write_bytes(original + b'changed')
        self.run_probe(expect=1)
        self.assertEqual(before, (digest(target), target.stat().st_mtime_ns))
        self.assertFalse(self.receipt.exists())
        self.apk.write_bytes(original)
        self.run_probe()
        self.assertEqual(before, (digest(target), target.stat().st_mtime_ns))

    def test_wrong_obb_preserves_existing_destination_and_retry(self):
        target = self.root / 'local/game/obb' / MAIN
        target.parent.mkdir(parents=True)
        target.write_bytes(b'previous data')
        original = self.main.read_bytes()
        self.main.write_bytes(bytes(len(original)))
        self.run_probe(expect=1)
        self.assertEqual(target.read_bytes(), b'previous data')
        self.assertFalse(self.receipt.exists())
        self.assertFalse(list(self.root.rglob('*.part-*')))
        self.main.write_bytes(original)
        self.run_probe()

    def test_cancellation_during_hash_extraction_and_bootstrap(self):
        for mode in ('cancel', 'cancel-write', 'cancel-bootstrap'):
            with self.subTest(mode=mode):
                self.root = self.base / mode
                self.root.mkdir()
                self.run_probe(mode, expect=14)
                self.assertFalse(self.receipt.exists())
                self.assertFalse(list(self.root.rglob('*.part-*')))
                self.run_probe()
                self.run_probe('ready')

    def test_changed_or_missing_resources_reopen_setup_and_repair(self):
        self.run_probe()
        target = self.resources / 'strings.gla'
        target.write_bytes(b'corruption')
        self.run_probe('ready', expect=10)
        self.run_probe(apk='-', data='-')
        self.assertEqual(target.read_bytes(), b'patch wins')
        target.unlink()
        self.run_probe('ready', expect=10)
        self.run_probe(apk='-', data='-')

    def test_receipt_tampering_is_not_accepted(self):
        self.run_probe()
        self.receipt.write_text('NOVA-SETUP-1\n')
        self.run_probe('ready', expect=10)

    def test_duplicate_members_rejected(self):
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            archive(self.apk, [(LIBRARY, self.library), (LIBRARY, self.library)])
        self.write_spec()
        self.run_probe(expect=1)
        self.assertFalse(self.receipt.exists())

    def test_corrupt_bootstrap_crc_does_not_publish(self):
        archive(self.main, [('strings.gla', b'old strings'), ('z1.etc', b'MAIN-DATA' * 50)], zipfile.ZIP_STORED)
        content = bytearray(self.main.read_bytes())
        content[content.index(b'MAIN-DATA')] ^= 1
        self.main.write_bytes(content)
        self.write_spec()
        self.run_probe(expect=1)
        self.assertFalse(self.receipt.exists())
        self.assertFalse((self.resources / 'z1.etc').exists())
        self.assertFalse(list(self.root.rglob('*.part-*')))

    def test_truncated_archive_rejected(self):
        self.apk.write_bytes(self.apk.read_bytes()[:-10])
        self.write_spec()
        self.run_probe(expect=1)

    def test_encrypted_member_rejected(self):
        content = bytearray(self.apk.read_bytes())
        central = content.index(b'PK\x01\x02')
        content[6] |= 1
        content[central + 8] |= 1
        self.apk.write_bytes(content)
        self.write_spec()
        self.run_probe(expect=1)

    def test_output_junction_is_rejected(self):
        outside = self.base / 'outside'
        outside.mkdir()
        link = self.root / 'local'
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(outside)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        try:
            self.run_probe(expect=1)
            self.assertEqual(list(outside.iterdir()), [])
        finally:
            os.rmdir(link)

    def test_quoting_and_actual_hidden_child_launch(self):
        result = subprocess.run([str(PROBE), 'quote'], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(PROBE), 'launch', str(self.root), str(CHILD)], capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / 'child-started.txt').exists())
        logs = list((self.root / 'local/logs/launcher').glob('game-*.log'))
        self.assertEqual(len(logs), 1)
        self.assertIn(b'synthetic host stdout', logs[0].read_bytes())
        (self.root / 'fail-child').touch()
        result = subprocess.run([str(PROBE), 'launch', str(self.root), str(CHILD)], capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 7, result.stderr)

    def test_system_language_mapping_and_complete_catalogues(self):
        result = subprocess.run([str(PROBE), 'language'], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_local_artwork_icon_and_shortcut_preservation(self):
        # Generated 2x2 RGBA fixture, not original game artwork.
        def png_chunk(name, data):
            return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data))
        png = b'\x89PNG\r\n\x1a\n'
        png += png_chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0))
        png += png_chunk(b'IDAT', zlib.compress((b'\0' + b'\x20\xe0\xf0\xff' * 2) * 2))
        png += png_chunk(b'IEND', b'')
        assets = self.root / 'local/launcher'
        assets.mkdir(parents=True)
        (assets / 'icon.png').write_bytes(png)
        launcher = self.root / 'launcher.exe'
        launcher.write_bytes(b'synthetic path only')
        command = [str(PROBE), 'shortcut', str(self.root), str(launcher)]
        result = subprocess.run(command, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        ico = assets / 'game.ico'
        content = ico.read_bytes()
        self.assertEqual(content[:6], b'\0\0\x01\0\x01\0')
        self.assertEqual(content[22:30], b'\x89PNG\r\n\x1a\n')
        link = self.root / 'N.O.V.A. 3.lnk'
        self.assertTrue(link.is_file())
        times = (ico.stat().st_mtime_ns, link.stat().st_mtime_ns)
        result = subprocess.run(command, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(times, (ico.stat().st_mtime_ns, link.stat().st_mtime_ns))


if __name__ == '__main__':
    unittest.main(verbosity=2)
