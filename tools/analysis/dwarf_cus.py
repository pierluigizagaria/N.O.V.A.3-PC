import sys
from collections import Counter

from elftools.elf.elffile import ELFFile


def main(path, limit="40"):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for sec in elf.iter_sections():
            if sec.name.startswith(".debug") or sec.name in (".text", ".symtab"):
                print("%-16s %10d bytes" % (sec.name, sec["sh_size"]))
        if not elf.has_dwarf_info():
            print("no DWARF")
            return
        dwarf = elf.get_dwarf_info()
        names = []
        producers = Counter()
        for cu in dwarf.iter_CUs():
            top = cu.get_top_DIE()
            name = top.attributes.get("DW_AT_name")
            comp = top.attributes.get("DW_AT_comp_dir")
            prod = top.attributes.get("DW_AT_producer")
            names.append((name.value.decode(errors="replace") if name else "?",
                          comp.value.decode(errors="replace") if comp else ""))
            if prod:
                producers[prod.value.decode(errors="replace")[:80]] += 1
        print("compile units:", len(names))
        for p, n in producers.most_common(5):
            print("producer %4d  %s" % (n, p))
        dirs = Counter()
        for n, c in names:
            parts = n.replace("\\", "/").split("/")
            dirs["/".join(parts[:-1][-3:])] += 1
        for d, n in dirs.most_common(int(limit)):
            print("%4d  %s" % (n, d))
        print("sample:", "; ".join(n for n, _ in names[:15]))
        print("comp_dir sample:", names[0][1] if names else "")


if __name__ == "__main__":
    main(*sys.argv[1:])
