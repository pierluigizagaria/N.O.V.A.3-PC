"""Synthetic tests for tools/package_release.py; uses fake PE images only."""
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_release  # noqa: E402
import release  # noqa: E402


def fake_pe(path, machine=0x14c):
    header = bytearray(256)
    header[:2] = b'MZ'
    struct.pack_into('<I', header, 0x3c, 0x80)
    header[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', header, 0x84, machine)
    path.write_bytes(bytes(header) + b'synthetic')


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.build = self.base / 'build'
        self.build.mkdir()
        for name in ('nova3pc-launcher.exe', 'nova3pc.exe', 'SDL3.dll', 'libEGL.dll', 'libGLESv2.dll', 'zlib1.dll'):
            fake_pe(self.build / name)
        (self.build / 'nova3pc.pdb').write_bytes(b'debug symbols stay out')
        self.background = self.base / 'background-a.png'
        self.background.write_bytes(b'\x89PNG\r\n\x1a\nsynthetic')

    def run_package(self, **overrides):
        options = dict(build_dir=self.build, output=self.base / 'dist', name='N.O.V.A.3-PC',
                       background=self.background, make_zip=True)
        options.update(overrides)
        return package_release.package(**options)

    def test_portable_folder_and_zip(self):
        target, archive = self.run_package()
        # The packager resolves paths; hosted Windows TEMP can use RUNNER~1.
        self.assertEqual(archive, (self.base / 'dist/N.O.V.A.3-PC.zip').resolve())
        names = sorted(p.relative_to(target).as_posix() for p in target.rglob('*') if p.is_file())
        self.assertEqual(names, ['LEGGIMI - README.txt', 'NOVA3-Game.exe', 'NOVA3.exe', 'SDL3.dll',
                                 'SHA256SUMS.txt', 'libEGL.dll', 'libGLESv2.dll',
                                 'local/launcher/background-a.png', 'server.conf', 'zlib1.dll'])
        sums = (target / 'SHA256SUMS.txt').read_text()
        self.assertIn('NOVA3.exe', sums)
        with zipfile.ZipFile(archive) as packed:
            self.assertIn('N.O.V.A.3-PC/NOVA3.exe', packed.namelist())
            self.assertFalse([n for n in packed.namelist() if n.endswith('.pdb')])

    def test_cli_archive_names_and_repackaging(self):
        for name in ('N.O.V.A.3-PC', 'N.O.V.A.3-PC-v0.1.3-win32', 'portable'):
            with self.subTest(name=name):
                output = self.base / name / 'dist'
                archive = output / (name + '.zip')
                output.mkdir(parents=True)
                archive.write_bytes(b'stale archive')
                result = subprocess.run(
                    [sys.executable, str(Path(package_release.__file__)),
                     '--build-dir', str(self.build), '--output', str(output),
                     '--name', name, '--no-background', '--zip'],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(list(output.glob('*.zip')), [archive])
                with zipfile.ZipFile(archive) as packed:
                    self.assertIsNone(packed.testzip())
                    self.assertIn(f'{name}/NOVA3.exe', packed.namelist())

    @unittest.skipUnless(os.name == 'nt', 'Windows 8.3 path regression')
    def test_windows_short_temp_path(self):
        import ctypes
        short_path = ctypes.WinDLL('kernel32', use_last_error=True).GetShortPathNameW
        short_path.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32]
        short_path.restype = ctypes.c_uint32
        buffer = ctypes.create_unicode_buffer(32768)
        length = short_path(str(self.base), buffer, len(buffer))
        self.assertGreater(length, 0, f'GetShortPathNameW: {ctypes.get_last_error()}')
        self.assertLess(length, len(buffer))
        if buffer.value.casefold() == str(self.base.resolve()).casefold():
            self.skipTest('8.3 aliases are disabled on the temporary volume')
        # A fresh interpreter reads TEMP just like the hosted Windows runner.
        env = dict(os.environ, TEMP=buffer.value, TMP=buffer.value, TMPDIR=buffer.value)
        result = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()),
             'PackageTests.test_portable_folder_and_zip'],
            env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_logos_only_on_request(self):
        art = self.base / 'art'
        art.mkdir()
        for logo in package_release.LOGOS:
            (art / logo).write_bytes(b'\x89PNG\r\n\x1a\nsynthetic')
        (art / 'neuropol.ttf').write_bytes(b'not packaged')
        target, _ = self.run_package(make_zip=False)
        self.assertFalse((target / 'local/launcher/logo-pc.png').exists())
        target, _ = self.run_package(make_zip=False, logos=art)
        self.assertTrue((target / 'local/launcher/logo-pc.png').is_file())
        self.assertTrue((target / 'local/launcher/icon.png').is_file())
        self.assertFalse((target / 'local/launcher/neuropol.ttf').exists())
        (art / 'icon.png').unlink()
        with self.assertRaises(SystemExit):
            self.run_package(make_zip=False, logos=art)

    def test_without_background(self):
        target, _ = self.run_package(background=None, make_zip=False)
        self.assertFalse((target / 'local').exists())

    def test_release_branding_fallback_and_missing_icon(self):
        local = self.base / 'local-art'
        approved = self.base / 'approved-art'
        local.mkdir()
        approved.mkdir()
        for name in ('background-a.png', *package_release.LOGOS):
            (approved / name).write_bytes(b'approved synthetic PNG')
        (local / 'logo-pc.png').write_bytes(b'incomplete local artwork')
        background, logos = release.branding(local, approved)
        self.assertEqual(background, approved / 'background-a.png')
        self.assertEqual(logos, approved)
        target, _ = self.run_package(background=background, logos=logos)
        self.assertEqual((target / 'local/launcher/logo-pc.png').read_bytes(),
                         (approved / 'logo-pc.png').read_bytes())
        (approved / 'icon.png').unlink()
        with self.assertRaisesRegex(SystemExit, 'missing original logo/icon'):
            release.branding(local, approved)
        self.assertIsNone(release.branding(local, approved, with_logos=False)[1])

    def test_server_configuration_uses_only_public_offline_template(self):
        (self.build / 'server.conf').write_text('mode=remote\naddress=private\nsecret=do-not-ship\n')
        target, archive = self.run_package()
        template = (package_release.ROOT / 'assets/server.conf').read_bytes()
        self.assertEqual((target / 'server.conf').read_bytes(), template)
        active = [line for line in template.decode().splitlines() if line and not line.startswith('#')]
        self.assertEqual(active, ['mode = offline'])
        with zipfile.ZipFile(archive) as packed:
            self.assertEqual(packed.read('N.O.V.A.3-PC/server.conf'), template)

    def test_repackaging_replaces_previous_folder(self):
        target, _ = self.run_package(make_zip=False)
        (target / 'stale.txt').write_text('old')
        target, _ = self.run_package(make_zip=False)
        self.assertFalse((target / 'stale.txt').exists())

    def test_game_files_and_wrong_architecture_are_refused(self):
        fake_pe(self.build / 'x64.dll', machine=0x8664)
        with self.assertRaises(SystemExit):
            self.run_package(make_zip=False)
        (self.build / 'x64.dll').unlink()
        with self.assertRaises(SystemExit):
            self.run_package(background=self.base / 'neuropol.ttf', make_zip=False)
        (self.build / 'nova3pc.exe').unlink()
        with self.assertRaises(SystemExit):
            self.run_package(make_zip=False)


if __name__ == '__main__':
    unittest.main(verbosity=2)
