"""Build synthetic ELF32 fixtures locally with VS clang/LLD; no Android SDK needed."""
import argparse
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--llvm-bin', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output / 'loader.so'
    host = args.output / 'fixture-host.so'
    subprocess.run([str(args.llvm_bin / 'clang.exe'), '--target=i686-linux-android',
                    '-shared', '-nostdlib', '-fPIC',
                    '--ld-path=' + str(args.llvm_bin / 'ld.lld.exe'),
                    '-Wl,-soname,fixture-host.so', str(args.source / 'host.c'),
                    '-o', str(host)], check=True)
    subprocess.run([
        str(args.llvm_bin / 'clang.exe'), '--target=i686-linux-android',
        '-shared', '-nostdlib', '-fPIC', '-fno-stack-protector', '-fno-builtin',
        '-fno-omit-frame-pointer', '-O1', '-g',
        '--ld-path=' + str(args.llvm_bin / 'ld.lld.exe'),
        '-Wl,--hash-style=sysv', '-Wl,-init,fixture_init',
        str(args.source / 'loader.c'), str(args.source / 'boundary.S'), str(host), '-o', str(output),
    ], check=True)
    subprocess.run([
        str(args.llvm_bin / 'clang.exe'), '--target=i686-linux-android',
        '-shared', '-nostdlib', '-fPIC', '-fno-stack-protector', '-fno-builtin',
        '-fno-omit-frame-pointer', '-O1', '-g',
        '--ld-path=' + str(args.llvm_bin / 'ld.lld.exe'), '-Wl,--hash-style=sysv',
        '-Wl,-Bsymbolic', str(args.source / 'bionic.c'),
        str(args.source / 'bionic_boundary.S'), '-o', str(args.output / 'bionic.so'),
    ], check=True)
    subprocess.run([
        str(args.llvm_bin / 'clang.exe'), '--target=i686-linux-android',
        '-shared', '-nostdlib', '-fPIC', '-fno-stack-protector', '-fno-builtin',
        '-fno-omit-frame-pointer', '-O1', '-g',
        '--ld-path=' + str(args.llvm_bin / 'ld.lld.exe'), '-Wl,--hash-style=sysv',
        '-Wl,-Bsymbolic', str(args.source / 'platform.c'), '-o', str(args.output / 'platform.so'),
    ], check=True)
    subprocess.run([
        str(args.llvm_bin / 'clang.exe'), '--target=i686-linux-android',
        '-shared', '-nostdlib', '-fPIC', '-fno-stack-protector', '-fno-builtin',
        '-fno-omit-frame-pointer', '-O1', '-g',
        '--ld-path=' + str(args.llvm_bin / 'ld.lld.exe'), '-Wl,--hash-style=sysv',
        '-Wl,-Bsymbolic', str(args.source / 'media.c'), str(args.source / 'audio.c'),
        str(args.source / 'media_boundary.S'), '-o', str(args.output / 'media.so'),
    ], check=True)
    original = output.read_bytes()
    subprocess.run([
        str(args.llvm_bin / 'clang.exe'), '--target=i686-linux-android',
        '-shared', '-nostdlib', '-fPIC', '-fno-stack-protector', '-fno-builtin',
        '-fno-omit-frame-pointer', '-O1', '-g',
        '--ld-path=' + str(args.llvm_bin / 'ld.lld.exe'), '-Wl,--hash-style=sysv',
        '-Wl,-Bsymbolic', str(args.source / 'world_fov_contract.c'),
        str(args.source / 'world_fov_boundary.S'), '-o', str(args.output / 'world_fov.so'),
    ], check=True)
    subprocess.run([
        str(args.llvm_bin / 'clang.exe'), '--target=i686-linux-android',
        '-shared', '-nostdlib', '-fPIC', '-fno-stack-protector', '-fno-builtin',
        '-fno-omit-frame-pointer', '-O1', '-g',
        '--ld-path=' + str(args.llvm_bin / 'ld.lld.exe'), '-Wl,--hash-style=sysv',
        '-Wl,-Bsymbolic', str(args.source / 'input.c'), str(args.source / 'input_boundary.S'),
        str(args.source / 'look_contract.c'), str(args.source / 'draw_contract.c'), str(args.source / 'mech_contract.c'),
        str(args.source / 'machinegun_contract.c'), str(args.source / 'mounted_machinegun_contract.c'),
        str(args.source / 'bike_contract.c'), str(args.source / 'bike_boundary.S'), str(args.source / 'driver_contract.c'), str(args.source / 'jeep_contract.c'),
        '-o', str(args.output / 'input.so'),
    ], check=True)
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', original)
    phoff, phentsize, phnum = header[5], header[9], header[10]
    shoff, shentsize, shnum = header[6], header[11], header[12]
    sections = [struct.unpack_from('<10I', original, shoff + i * shentsize) for i in range(shnum)]
    rel = next(s for s in sections if s[1] == 9 and s[5] >= 8)
    bad_relocation = bytearray(original)
    info = struct.unpack_from('<I', bad_relocation, rel[4] + 4)[0]
    struct.pack_into('<I', bad_relocation, rel[4] + 4, (info & ~255) | 42)
    (args.output / 'bad_relocation.so').write_bytes(bad_relocation)
    bad_target = bytearray(original)
    struct.pack_into('<I', bad_target, rel[4], 0xfffffff0)
    (args.output / 'bad_target.so').write_bytes(bad_target)
    bad_symbol = bytearray(original)
    struct.pack_into('<I', bad_symbol, rel[4] + 4, (0xffffff << 8) | 1)
    (args.output / 'bad_symbol.so').write_bytes(bad_symbol)
    programs = [struct.unpack_from('<8I', original, phoff + i * phentsize) for i in range(phnum)]
    load_index = next(i for i, p in enumerate(programs) if p[0] == 1 and p[6] & 1)
    wx = bytearray(original)
    struct.pack_into('<I', wx, phoff + load_index * phentsize + 24, 7)
    (args.output / 'wx.so').write_bytes(wx)
    wrong_machine = bytearray(original)
    struct.pack_into('<H', wrong_machine, 18, 40)
    (args.output / 'arm.so').write_bytes(wrong_machine)
    (args.output / 'truncated.so').write_bytes(original[:70])
    stripped = bytearray(original)
    struct.pack_into('<I', stripped, 32, 0)
    struct.pack_into('<HHH', stripped, 46, 0, 0, 0)
    (args.output / 'stripped.so').write_bytes(stripped)


if __name__ == '__main__':
    main()
