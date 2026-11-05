import bisect
import sys
from collections import Counter

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from elftools.elf.elffile import ELFFile


def main(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        text = elf.get_section_by_name(".text")
        code = text.data()
        base = text["sh_addr"]
        symtab = elf.get_section_by_name(".symtab")
        funcs = sorted((s["st_value"], s["st_size"], s.name) for s in symtab.iter_symbols()
                       if s["st_info"]["type"] == "STT_FUNC" and s["st_value"])
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    kinds = Counter()
    owners = Counter()
    total = 0
    # sweep each function body separately so data between functions cannot desync the decoder
    for start, size, name in funcs:
        off = start - base
        if size == 0 or off < 0 or off >= len(code):
            continue
        body = code[off:off + size]
        for addr, sz, mnem, ops in md.disasm_lite(body, start):
            total += 1
            if mnem in ("movaps", "movdqa", "movapd") and ("esp" in ops or "ebp" in ops):
                kinds["%s %s" % (mnem, ops)] += 1
                owners[name] += 1
    print("instructions decoded:", total)
    print("aligned SSE stack accesses:", sum(kinds.values()), "in", len(owners), "functions")
    for k, n in kinds.most_common(12):
        print("  %5d  %s" % (n, k))
    for k, n in owners.most_common(8):
        print("  owner %4d  %s" % (n, k[:90]))


if __name__ == "__main__":
    main(sys.argv[1])
