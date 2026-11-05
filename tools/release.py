"""One-command release: configure, build, optionally test, then package.

Run from a normal command prompt on Windows (Visual Studio 18 with the C++
x86 and Clang components installed); CMake is found on PATH or through
vswhere. Output: dist/N.O.V.A.3-PC/ and dist/N.O.V.A.3-PC.zip.

    release.cmd                      (wrapper in the repository root)
    python tools/release.py --test   (also run the launcher test suite)
    python tools/release.py --apk "C:\\path\\N.O.V.A. 3.apk"

--apk is read in place and only used to import the original icon into
local/launcher/ when it is not there yet, so both executables get the game
icon. The original logo and icon are packaged unless --no-logos is given.
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import package_release  # noqa: E402

ARTWORK = ROOT / 'local/launcher'
APK_ICON = 'res/drawable-xxhdpi/icon.png'


def step(message):
    print(f'\n==> {message}', flush=True)


def find_cmake():
    found = shutil.which('cmake')
    if found:
        return found
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / \
        'Microsoft Visual Studio/Installer/vswhere.exe'
    if vswhere.is_file():
        result = subprocess.run([str(vswhere), '-latest', '-prerelease', '-products', '*', '-find',
                                 r'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'],
                                capture_output=True, text=True)
        for line in result.stdout.splitlines():
            if Path(line).is_file():
                return line
    raise SystemExit('release: CMake not found. Install Visual Studio 18 with "C++ CMake tools".')


def import_icon(apk):
    target = ARTWORK / 'icon.png'
    if target.is_file():
        return
    if not apk:
        print('note: local/launcher/icon.png is missing; executables get no icon. '
              'Pass --apk or run the launcher setup once.')
        return
    with zipfile.ZipFile(apk) as archive:
        try:
            data = archive.read(APK_ICON)
        except KeyError:
            raise SystemExit(f'release: {apk} has no {APK_ICON}; is it the N.O.V.A. 3 APK?')
    ARTWORK.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    print(f'imported icon from {Path(apk).name}')


def run(*command):
    print('  ' + ' '.join(str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description='Build and package N.O.V.A. 3 PC.')
    parser.add_argument('--apk', help='original APK, only to import the icon (read in place)')
    parser.add_argument('--test', action='store_true', help='run the launcher tests before packaging')
    parser.add_argument('--no-logos', action='store_true', help='do not package the original logo and icon')
    parser.add_argument('--no-zip', action='store_true')
    parser.add_argument('--output', default=ROOT / 'dist')
    args = parser.parse_args()

    step('Artwork')
    import_icon(args.apk)
    cmake = find_cmake()
    ctest = str(Path(cmake).with_name('ctest.exe' if os.name == 'nt' else 'ctest'))

    step('Configure (x86, clang-cl, vcpkg)')
    run(cmake, '--preset', 'win32')
    step('Build Release')
    targets = [] if args.test else ['--target', 'nova3pc', 'nova3pc-launcher']
    run(cmake, '--build', '--preset', 'release', *targets)
    if args.test:
        step('Test launcher')
        run(ctest, '--preset', 'release', '-R', 'launcher')

    step('Package')
    background = ARTWORK / 'background-a.png'
    logos = None
    if not args.no_logos:
        if all((ARTWORK / name).is_file() for name in package_release.LOGOS):
            logos = ARTWORK
        else:
            print('note: original logo/icon not in local/launcher; packaging without them.')
    target, archive = package_release.package(
        ROOT / 'build/win32/Release', args.output, 'N.O.V.A.3-PC',
        background if background.is_file() else None, not args.no_zip, logos)
    print(f'\nDone: {target}' + (f'\n      {archive}' if archive else ''))


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f'release: step failed with exit code {error.returncode}')
