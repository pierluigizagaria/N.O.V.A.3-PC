"""Build a shareable, portable N.O.V.A. 3 PC folder (and optional ZIP).

The package contains our executables, their runtime DLLs, a short readme and,
optionally, the launcher background (our own generated artwork). Game data is
never included: users drag their own APK and data onto the launcher on first
start, which imports them into local/. The original N.O.V.A. 3 logo and icon
(extracted from the game) are added only with --with-logos, an explicit
owner-approved branding exception for these two files only.

Usage (from the repository root, after `cmake --build --preset release`):
    python tools/package_release.py
    python tools/package_release.py --build-dir build/win32/Release --zip
"""
import argparse
import hashlib
import shutil
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Build output name -> shipped name. NOVA3.exe is the only file users start.
EXECUTABLES = {'nova3pc-launcher.exe': 'NOVA3.exe', 'nova3pc.exe': 'NOVA3-Game.exe'}
# Extensions and names that indicate game content or material extracted from it.
FORBIDDEN_SUFFIXES = {'.apk', '.obb', '.so', '.dex', '.gla', '.etc', '.ttf', '.ico', '.pdb'}
FORBIDDEN_NAMES = {'logo-original.png', 'icon.png', 'neuropol.ttf', 'game.ico', 'ready.txt'}
# Original branding that --with-logos may add; nothing else extracted is ever allowed.
LOGOS = ('logo-original.png', 'icon.png')

README = """N.O.V.A. 3 PC - unofficial port
================================

[IT] Avvio
1. Estrai questa cartella dove vuoi (anche su una chiavetta).
2. Avvia "NOVA3.exe".
3. Al primo avvio trascina nella finestra il tuo APK di N.O.V.A. 3 1.0.7 x86
   (oppure premi "Seleziona APK"). Se i dati main/patch non sono accanto
   all'APK, il launcher ti chiede lo ZIP dei dati o uno dei due OBB.
4. I file vengono verificati ed estratti nella cartella "local". Poi il gioco
   parte da solo. Dagli avvii successivi il gioco si apre direttamente.
   Per rifare la configurazione: NOVA3.exe --setup

[EN] Getting started
1. Extract this folder anywhere (a USB drive works too).
2. Run "NOVA3.exe".
3. On first start, drag your N.O.V.A. 3 1.0.7 x86 APK onto the window (or press
   "Select APK"). If the main/patch data is not next to the APK, the launcher
   asks for the data ZIP or either OBB.
4. Files are verified and extracted into the "local" folder, then the game
   starts by itself. Later starts open the game directly.
   To run setup again: NOVA3.exe --setup

This package contains no game files. You must own the original game.
Everything stays on your PC; the original files you select are never modified.
"""


def fail(message):
    raise SystemExit(f'package_release: {message}')


def pe_machine(path):
    """Return the COFF machine of a PE image, or None if it is not one."""
    data = path.read_bytes()[:4096]
    if len(data) < 64 or data[:2] != b'MZ':
        return None
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if offset + 6 > len(data) or data[offset:offset + 4] != b'PE\0\0':
        return None
    return struct.unpack_from('<H', data, offset + 4)[0]


def check_payload(path, allowed=()):
    name = path.name.lower()
    if name in allowed:
        return
    if path.suffix.lower() in FORBIDDEN_SUFFIXES or name in FORBIDDEN_NAMES:
        fail(f'refusing to package {path.name}: game or local-only file')


def package(build_dir, output, name, background, make_zip, logos=None):
    build_dir = Path(build_dir).resolve()
    for exe in EXECUTABLES:
        if not (build_dir / exe).is_file():
            fail(f'missing {exe} in {build_dir}; build the Release preset first')
    files = [build_dir / exe for exe in EXECUTABLES] + sorted(build_dir.glob('*.dll'))
    shipped = {build_dir / source: name for source, name in EXECUTABLES.items()}
    for item in files:
        check_payload(item)
        if pe_machine(item) != 0x14c:
            fail(f'{item.name} is not a 32-bit x86 Windows image')

    target = Path(output).resolve() / name
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for item in files:
        shutil.copy2(item, target / shipped.get(item, item.name))
    (target / 'LEGGIMI - README.txt').write_text(README, encoding='utf-8', newline='\r\n')
    if background:
        background = Path(background)
        if background.suffix.lower() != '.png':
            fail('the launcher background must be a PNG file')
        art = target / 'local/launcher/background-a.png'
        art.parent.mkdir(parents=True)
        shutil.copy2(background, art)
    allowed = ()
    if logos:
        logos = Path(logos)
        for logo in LOGOS:
            if not (logos / logo).is_file():
                fail(f'--with-logos needs {logo} in {logos}')
            destination = target / 'local/launcher' / logo
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(logos / logo, destination)
        allowed = LOGOS

    sums = []
    for item in sorted(p for p in target.rglob('*') if p.is_file()):
        check_payload(item, allowed)
        relative = item.relative_to(target).as_posix()
        sums.append(f'{hashlib.sha256(item.read_bytes()).hexdigest()}  {relative}')
    (target / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n', encoding='ascii')

    archive = None
    if make_zip:
        # The product name contains dots; append instead of replacing '.3-PC'.
        archive = target.with_name(target.name + '.zip')
        archive.unlink(missing_ok=True)
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as out:
            for item in sorted(p for p in target.rglob('*') if p.is_file()):
                out.write(item, f'{name}/{item.relative_to(target).as_posix()}')
    return target, archive


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--build-dir', default=ROOT / 'build/win32/Release')
    parser.add_argument('--output', default=ROOT / 'dist')
    parser.add_argument('--name', default='N.O.V.A.3-PC')
    parser.add_argument('--background', default=ROOT / 'local/launcher/background-a.png',
                        help='launcher background PNG (generated artwork, not a game file)')
    parser.add_argument('--no-background', action='store_true')
    parser.add_argument('--zip', action='store_true', help='also write <name>.zip')
    parser.add_argument('--with-logos', action='store_true',
                        help='include logo-original.png and icon.png (original game art)')
    parser.add_argument('--logos-dir', default=ROOT / 'local/launcher',
                        help='folder holding logo-original.png and icon.png (default local/launcher)')
    args = parser.parse_args(argv)
    background = None if args.no_background else Path(args.background)
    if background and not background.is_file():
        print(f'note: {background} not found; the launcher will use its plain backdrop', file=sys.stderr)
        background = None
    logos = Path(args.logos_dir) if args.with_logos else None
    target, archive = package(args.build_dir, args.output, args.name, background, args.zip, logos)
    print(f'package: {target}')
    if archive:
        print(f'archive: {archive}')


if __name__ == '__main__':
    main()
