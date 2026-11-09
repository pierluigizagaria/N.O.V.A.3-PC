"""Capture bounded x86 thread stacks/state for a live game; never dump memory.

Run with a 64-bit Python and an explicit PID. Thread suspension covers only
context/stack reads and always resumes in finally. Symbolization is read-only.
"""
import argparse
import bisect
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int)
    parser.add_argument("--thread", type=int)
    parser.add_argument("--host-base", type=lambda v: int(v, 0), default=0)
    args = parser.parse_args()
    k = c.WinDLL("kernel32", use_last_error=True)
    signatures = {
        "OpenProcess": ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        "OpenThread": ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        "CloseHandle": ([w.HANDLE], w.BOOL),
        "ReadProcessMemory": ([w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)], w.BOOL),
        "VirtualQueryEx": ([w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t], c.c_size_t),
        "CreateToolhelp32Snapshot": ([w.DWORD, w.DWORD], w.HANDLE),
        "Thread32First": ([w.HANDLE, c.c_void_p], w.BOOL),
        "Thread32Next": ([w.HANDLE, c.c_void_p], w.BOOL),
        "SuspendThread": ([w.HANDLE], w.DWORD),
        "ResumeThread": ([w.HANDLE], w.DWORD),
        "Wow64GetThreadContext": ([w.HANDLE, c.c_void_p], w.BOOL),
    }
    for name, (parameters, result) in signatures.items():
        getattr(k, name).argtypes, getattr(k, name).restype = parameters, result
    process = k.OpenProcess(0x410, False, args.pid)
    if not process:
        raise c.WinError(c.get_last_error())

    def read(address, size):
        b = c.create_string_buffer(size)
        n = c.c_size_t()
        if not k.ReadProcessMemory(process, address, b, size, c.byref(n)):
            return b""
        return b.raw[:n.value]

    def word(address):
        value = read(address, 4)
        return struct.unpack("<I", value)[0] if len(value) == 4 else 0

    try:
        base = 0
        address = 0
        while address < 0x100000000:
            mbi = c.create_string_buffer(48)
            if not k.VirtualQueryEx(process, address, mbi, 48):
                break
            region, allocation = struct.unpack_from("<QQ", mbi.raw)
            size = struct.unpack_from("<Q", mbi.raw, 24)[0]
            state, protection = struct.unpack_from("<II", mbi.raw, 32)
            if state == 0x1000 and not protection & 0x101 and read(region, 4) == b"\x7fELF":
                if read(region + 0x403720, 5) == b"\x55\x31\xc0\x89\xe5":
                    base = region
                    break
            address = region + size
        print(f"PID={args.pid} ELF=0x{base:x}")
        if not base:
            raise RuntimeError("Expected game image not found")
        app = word(base + 0x115c08c)
        stack = app + 0x18738
        index = word(stack + 0x34)
        state = word(stack + 4 * index) if index < 12 else 0
        print(f"application=0x{app:x} state_index={index} state=0x{state:x} vtable=0x{word(state)-base:x} step={word(state+0x30)} elapsed={word(state+0x34)}")
        records = []
        snapshot = k.CreateToolhelp32Snapshot(4, 0)
        try:
            entry = c.create_string_buffer(28)
            struct.pack_into("<I", entry, 0, 28)
            more = k.Thread32First(snapshot, entry)
            while more:
                tid, owner = struct.unpack_from("<II", entry.raw, 8)
                if owner == args.pid and (args.thread is None or tid == args.thread):
                    thread = k.OpenThread(0x4A, False, tid)
                    if thread:
                        suspended = False
                        try:
                            if k.SuspendThread(thread) == 0xffffffff:
                                raise c.WinError(c.get_last_error())
                            suspended = True
                            context = c.create_string_buffer(716)
                            struct.pack_into("<I", context, 0, 0x10007)
                            if not k.Wow64GetThreadContext(thread, context):
                                raise c.WinError(c.get_last_error())
                            ebp, eip = struct.unpack_from("<II", context.raw, 180)
                            esp = struct.unpack_from("<I", context.raw, 196)[0]
                            frames = [eip]
                            fp = ebp
                            for _ in range(24):
                                if not esp <= fp < esp + 1024 * 1024:
                                    break
                                data = read(fp, 8)
                                if len(data) != 8:
                                    break
                                next_fp, ret = struct.unpack("<II", data)
                                frames.append(ret)
                                if next_fp <= fp:
                                    break
                                fp = next_fp
                            stack_region = c.create_string_buffer(48)
                            k.VirtualQueryEx(process, esp, stack_region, 48)
                            stack_base = struct.unpack_from("<Q", stack_region.raw)[0]
                            stack_size = struct.unpack_from("<Q", stack_region.raw, 24)[0]
                            raw = read(esp, min(65536, stack_base + stack_size - esp) & ~3)
                            candidates = [v[0] for v in struct.iter_unpack("<I", raw) if base <= v[0] < base + 0xc10000 or (args.host_base and args.host_base <= v[0] < args.host_base + 0x240000)]
                            records.append((tid, esp, ebp, frames, list(dict.fromkeys(candidates))[:100]))
                        finally:
                            if suspended:
                                k.ResumeThread(thread)
                            k.CloseHandle(thread)
                more = k.Thread32Next(snapshot, entry)
        finally:
            k.CloseHandle(snapshot)
        with Path("local/input/lib/x86/libNOVA3_neon.so").open("rb") as source:
            elf = ELFFile(source)
            symbols = sorted((s["st_value"], s["st_size"], s.name) for s in elf.get_section_by_name(".symtab").iter_symbols() if s["st_info"]["type"] == "STT_FUNC" and s["st_size"])
        addresses = [s[0] for s in symbols]

        def symbol(address):
            if args.host_base and args.host_base <= address < args.host_base + 0x2ca000:
                return f"0x{address:x} HOST=0x{address-args.host_base+0x400000:x}"
            offset = address - base
            index = bisect.bisect_right(addresses, offset) - 1
            if index >= 0:
                start, size, name = symbols[index]
                if start <= offset < start + size:
                    return f"0x{address:x} ELF+0x{offset:x} {name}+0x{offset-start:x}"
            return f"0x{address:x}"

        for tid, esp, ebp, frames, candidates in records:
            print(f"thread={tid} ESP=0x{esp:x} EBP=0x{ebp:x}")
            for frame in frames:
                print("  frame " + symbol(frame))
            for frame in candidates:
                print("  stack-candidate " + symbol(frame))
    finally:
        k.CloseHandle(process)


if __name__ == "__main__":
    main()
