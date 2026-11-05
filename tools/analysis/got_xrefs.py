"""Find i386 functions that reference given GOT-relative displacements ([ebx - X]).

Usage: python -I got_xrefs.py <lib.so> 0x45c 0x618 ...
Prints the global each GOT slot resolves to and the instructions using it.
Keep saved output under local/ (never commit disassembly).
"""
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from elftools.elf.elffile import ELFFile


def main(path, *disps):
    disps = [int(d, 16) for d in disps]
    with open(path, "rb") as f:
        elf = ELFFile(f)
        text = elf.get_section_by_name(".text")
        code, base = text.data(), text["sh_addr"]
        got_plt = elf.get_section_by_name(".got.plt")["sh_addr"]
        got = elf.get_section_by_name(".got")
        got_data, got_addr = got.data(), got["sh_addr"]
        syms = [s for s in elf.get_section_by_name(".symtab").iter_symbols()
                if s["st_info"]["type"] in ("STT_OBJECT", "STT_FUNC") and s["st_value"]]
        funcs = sorted((s["st_value"], s["st_size"], s.name) for s in syms if s["st_info"]["type"] == "STT_FUNC")
        objects = {s["st_value"]: s.name for s in syms if s["st_info"]["type"] == "STT_OBJECT"}
    print("GOT base (.got.plt) %#x" % got_plt)
    for d in disps:
        slot = got_plt - d
        off = slot - got_addr
        target = struct.unpack_from("<I", got_data, off)[0] if 0 <= off < len(got_data) else None
        print("slot -%#x at %#x -> %s %s" % (d, slot, hex(target) if target is not None else "?",
                                             objects.get(target, "") if target is not None else ""))
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    pats = ["ebx - %#x]" % d for d in disps]
    for start, size, name in funcs:
        body = code[start - base:start - base + size]
        hits = [(a, m, o) for a, _, m, o in md.disasm_lite(body, start) if any(p in o for p in pats)]
        if hits:
            print("== %s" % name[:120])
            for a, m, o in hits:
                print("   %08x %s %s" % (a, m, o))


if __name__ == "__main__":
    main(*sys.argv[1:])
