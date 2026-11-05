"""Verify user-owned inputs and atomically extract only known members into local/."""
import argparse
import hashlib
from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APK_HASH = '7f6a0e1add98a7bd2bba8138b57b5cec22783eeb0a03d0cf75c8f5404d2da372'
APK_MEMBERS = {
    'lib/x86/libNOVA3_neon.so': 'a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e',
    'lib/armeabi-v7a/libNOVA3_neon.so': 'f8ebfb93620b7226fe9565395f47d260d0016022f69fb47920ee6079aa9f9ac9',
    'lib/armeabi-v7a/libNOVA3_8_neon.so': '136220fb9bc8983c7d2823d7c282e7310a36af9e6648408108c37553c6400987',
    'classes.dex': '1881000215cd3692e049a62ef31f5882e9cf5e0e7b3e962ada6e5d1aa3b1709f',
}
OBB_MEMBERS = {
    'main.1050.com.gameloft.android.ANMP.GloftN3HM.obb': '0e5d025cbe1a4c51d6071993cf402166191bf69901a0e0d9896235aeda2b2c14',
    'patch.1070.com.gameloft.android.ANMP.GloftN3HM.obb': '3b1806a3180070f76695be3fe052b40429ab6dc8c9d33fd9c4baf6db2219e738',
}
CHUNK = 1024 * 1024


def sha256(file):
    digest = hashlib.sha256()
    with Path(file).open('rb') as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b''):
            digest.update(chunk)
    return digest.hexdigest()


def extract_verified(archive, member, destination, expected):
    destination = Path(destination)
    if destination.is_file() and sha256(destination) == expected:
        print(f'unchanged {destination}')
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, suffix='.part',
                                         delete=False) as output:
            temporary = Path(output.name)
            digest = hashlib.sha256()
            with archive.open(member) as source:
                for chunk in iter(lambda: source.read(CHUNK), b''):
                    digest.update(chunk)
                    output.write(chunk)
        actual = digest.hexdigest()
        if actual != expected:
            raise ValueError(f'SHA-256 mismatch for {member}: {actual}, expected {expected}')
        temporary.replace(destination)
        print(f'verified {destination} SHA-256={actual}')
        return True
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def setup(apk, obb_zip, with_obb=False, root=ROOT):
    actual = sha256(apk)
    if actual != APK_HASH:
        raise ValueError(f'APK SHA-256 mismatch: {actual}, expected {APK_HASH}')
    print(f'verified APK SHA-256={actual}')
    with zipfile.ZipFile(apk) as archive:
        for name, expected in APK_MEMBERS.items():
            extract_verified(archive, name, root / 'local/input' / name, expected)
    if with_obb:
        with zipfile.ZipFile(obb_zip) as archive:
            for basename, expected in OBB_MEMBERS.items():
                matches = [n for n in archive.namelist()
                           if n.replace('\\', '/').rsplit('/', 1)[-1] == basename]
                if len(matches) != 1:
                    raise ValueError(f'Expected exactly one {basename}, found {len(matches)}')
                extract_verified(archive, matches[0], root / 'local/game/obb' / basename, expected)


# The Android installer materializes these bootstrap resources before nativeStart.
# Other OBB members remain in their archives and are mounted by InitFromLogo.
BOOTSTRAP_PATTERNS = ("/oconf.bar", "/glasuffixes.txt", "/eventparameters.xml",
                      "/packedwaves_sounds_hi.txt", "/packedwavesdates_sounds_hi.txt",
                      ".gla", "z1.etc")
PACKAGE = "com.gameloft.android.ANMP.GloftN3HM"


def crc32_file(path):
    import zlib
    value = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b""):
            value = zlib.crc32(chunk, value)
    return value


def prepare_profile(profile, root=ROOT):
    """Replay the installer's local OBB extraction; game bytes stay under local/."""
    local = (root / "local").resolve()
    profile = Path(profile)
    if not profile.is_absolute():
        profile = root / profile
    profile = profile.resolve()
    if not profile.is_relative_to(local) or profile == local:
        raise ValueError("Prepared game data must stay in a profile under local/")
    obbs = root / "local/game/obb"
    names = list(OBB_MEMBERS)
    if len(names) != 2:
        raise ValueError("Expected main and patch OBBs")
    for name in names:
        actual = sha256(obbs / name)
        if actual != OBB_MEMBERS[name]:
            raise ValueError(f"OBB SHA-256 mismatch for {name}")
    target = profile / "sdcard/Android/data" / PACKAGE / "files"
    if not target.resolve().is_relative_to(local):
        raise ValueError("Profile data path escapes local/")
    changed = count = total = 0
    with zipfile.ZipFile(obbs / names[0]) as main, zipfile.ZipFile(obbs / names[1]) as patch:
        patch_names = set(patch.namelist())
        for original in main.infolist():
            name = original.filename
            if original.is_dir() or not any(p in ("/" + name).lower() for p in BOOTSTRAP_PATTERNS):
                continue
            parts = name.split("/")
            if (name.startswith("/") or any(p in ("", ".", "..") or ":" in p or "\\" in p
                                            or p.endswith((".", " ")) for p in parts)):
                raise ValueError(f"Unsafe OBB member: {name}")
            archive = patch if name in patch_names else main
            entry = archive.getinfo(name)
            output = target.joinpath(*parts)
            if not output.resolve().is_relative_to(local):
                raise ValueError(f"OBB member escapes local/: {name}")
            count += 1
            total += entry.file_size
            if output.is_file() and output.stat().st_size == entry.file_size and crc32_file(output) == entry.CRC:
                continue
            output.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".part", delete=False) as stream:
                    temporary = Path(stream.name)
                    with archive.open(entry) as source:
                        for chunk in iter(lambda: source.read(CHUNK), b""):
                            stream.write(chunk)
                # ZipExtFile verifies CRC at EOF; replace only after complete validation.
                temporary.replace(output)
                changed += 1
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
    target.mkdir(parents=True, exist_ok=True)
    (target / "nothing.txt").write_text("DoNotDeleteThisFile", encoding="ascii")
    print(f"prepared bootstrap data: files={count} changed={changed} bytes={total} profile={profile}")
    return count, changed, total

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', required=True, type=Path)
    parser.add_argument('--obb-zip', required=True, type=Path)
    parser.add_argument('--with-obb', action='store_true')
    parser.add_argument('--prepare-profile', type=Path, help='Replay bootstrap extraction into a profile under local/')
    args = parser.parse_args()
    try:
        setup(args.apk, args.obb_zip, args.with_obb)
        if args.prepare_profile:
            prepare_profile(args.prepare_profile)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f'input setup: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
