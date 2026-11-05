"""Test extraction with generated archives; no game files are required."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('setup_inputs', ROOT / 'tools/setup_inputs.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class SetupTests(unittest.TestCase):
    def test_atomic_hash_rejection_and_idempotence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = b'synthetic payload' * 100000
            expected = hashlib.sha256(payload).hexdigest()
            archive_path = root / 'test.zip'
            with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
                archive.writestr('nested/member', payload)
            target = root / 'output.bin'
            target.write_bytes(b'existing')
            with zipfile.ZipFile(archive_path) as archive:
                with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                    setup.extract_verified(archive, 'nested/member', target, '0' * 64)
                self.assertEqual(target.read_bytes(), b'existing')
                self.assertFalse(list(root.glob('*.part')))
                self.assertTrue(setup.extract_verified(archive, 'nested/member', target, expected))
                timestamp = target.stat().st_mtime_ns
                with mock.patch.object(archive, 'open', side_effect=AssertionError('no extraction on rerun')):
                    self.assertFalse(setup.extract_verified(archive, 'nested/member', target, expected))
                self.assertEqual(target.stat().st_mtime_ns, timestamp)

    def test_setup_obb_opt_in_and_apk_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            apk, obb = root / 'input.apk', root / 'input.zip'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('lib/x86/test.so', b'fixture')
            with zipfile.ZipFile(obb, 'w') as archive:
                archive.writestr('folder/test.obb', b'obb fixture')
            apk_entries = {'lib/x86/test.so': hashlib.sha256(b'fixture').hexdigest()}
            obb_entries = {'test.obb': hashlib.sha256(b'obb fixture').hexdigest()}
            with mock.patch.multiple(setup, APK_HASH=setup.sha256(apk), APK_MEMBERS=apk_entries,
                                     OBB_MEMBERS=obb_entries):
                setup.setup(apk, root / 'absent.zip', root=root)
                self.assertFalse((root / 'local/game').exists())
                setup.setup(apk, obb, True, root=root)
                self.assertEqual((root / 'local/game/obb/test.obb').read_bytes(), b'obb fixture')
                original = apk.read_bytes()
                with mock.patch.object(setup, 'APK_HASH', '0' * 64):
                    with self.assertRaisesRegex(ValueError, 'APK SHA-256 mismatch'):
                        setup.setup(apk, obb, root=root)
                self.assertEqual(apk.read_bytes(), original)

    def test_ambiguous_obb_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            apk, obb = root / 'empty.apk', root / 'duplicate.zip'
            with zipfile.ZipFile(apk, 'w'):
                pass
            with zipfile.ZipFile(obb, 'w') as archive:
                archive.writestr('one/a.obb', b'1')
                archive.writestr('two/a.obb', b'2')
            with mock.patch.multiple(setup, APK_HASH=setup.sha256(apk), APK_MEMBERS={},
                                     OBB_MEMBERS={'a.obb': '0' * 64}):
                with self.assertRaisesRegex(ValueError, 'Expected exactly one'):
                    setup.setup(apk, obb, True, root=root)

    def test_bootstrap_patch_precedence_filter_and_local_only(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            obbs = root / "local/game/obb"
            obbs.mkdir(parents=True)
            main, patch = obbs / "main.obb", obbs / "patch.obb"
            with zipfile.ZipFile(main, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("sprites.gla", b"main")
                archive.writestr("eventParameters.xml", b"events")
                archive.writestr("level.bdae", b"leave in OBB")
            with zipfile.ZipFile(patch, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("sprites.gla", b"patched")
                archive.writestr("patch-only.gla", b"not enumerated by installer")
            with mock.patch.object(setup, "OBB_MEMBERS",
                                   {main.name: setup.sha256(main), patch.name: setup.sha256(patch)}):
                profile = Path("local/profiles/example")
                self.assertEqual(setup.prepare_profile(profile, root), (2, 2, 13))
                target = root / profile / "sdcard/Android/data" / setup.PACKAGE / "files"
                self.assertEqual((target / "sprites.gla").read_bytes(), b"patched")
                self.assertFalse((target / "level.bdae").exists())
                self.assertFalse((target / "patch-only.gla").exists())
                self.assertEqual(setup.prepare_profile(profile, root), (2, 0, 13))
                (target / "sprites.gla").write_bytes(b"corrupt")
                self.assertEqual(setup.prepare_profile(profile, root), (2, 1, 13))
                with self.assertRaisesRegex(ValueError, "under local"):
                    setup.prepare_profile(root / "outside", root)

    def test_bootstrap_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            obbs = root / "local/game/obb"
            obbs.mkdir(parents=True)
            main, patch = obbs / "main.obb", obbs / "patch.obb"
            with zipfile.ZipFile(main, "w") as archive:
                archive.writestr("../escape.gla", b"denied")
            with zipfile.ZipFile(patch, "w"):
                pass
            with mock.patch.object(setup, "OBB_MEMBERS",
                                   {main.name: setup.sha256(main), patch.name: setup.sha256(patch)}):
                with self.assertRaisesRegex(ValueError, "Unsafe OBB"):
                    setup.prepare_profile("local/profiles/example", root)


if __name__ == '__main__':
    unittest.main()
