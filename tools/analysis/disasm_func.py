"""Disassemble named functions of an i386 ELF, annotating calls with symbol names.

Usage: python -I disasm_func.py <lib.so> <substring> [<substring> ...]
Output goes to stdout; keep it under local/ if saved (never commit disassembly).
"""
import bisect
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def main(path, *needles):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        text = elf.get_section_by_name(".text")
        code, base = text.data(), text["sh_addr"]
        funcs = sorted((s["st_value"], s["st_size"], s.name)
                       for s in elf.get_section_by_name(".symtab").iter_symbols()
                       if s["st_info"]["type"] == "STT_FUNC" and s["st_value"])
        plt = {}
        dynsym = elf.get_section_by_name(".dynsym")
        pltsec = elf.get_section_by_name(".plt")
        for sec in elf.iter_sections():
            if isinstance(sec, RelocationSection) and sec.name == ".rel.plt":
                for i, r in enumerate(sec.iter_relocations()):
                    plt[pltsec["sh_addr"] + 16 * (i + 1)] = dynsym.get_symbol(r["r_info_sym"]).name
    addrs = [a for a, _, _ in funcs]

    def name_of(addr):
        if addr in plt:
            return plt[addr] + "@plt"
        j = bisect.bisect_right(addrs, addr) - 1
        if j >= 0:
            a, size, n = funcs[j]
            return "%s+%#x" % (n, addr - a)
        return "?"

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for start, size, name in funcs:
        if not any(n in name for n in needles):
            continue
        print("== %s @ %#x size %d" % (name, start, size))
        for insn in md.disasm(code[start - base:start - base + size], start):
            note = ""
            if insn.mnemonic in ("call", "jmp") and insn.op_str.startswith("0x"):
                note = "  ; " + name_of(int(insn.op_str, 16))
            print("  %08x  %-6s %s%s" % (insn.address, insn.mnemonic, insn.op_str, note))


if __name__ == "__main__":
    main(*sys.argv[1:])
