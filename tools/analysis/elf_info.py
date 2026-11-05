import sys
from collections import Counter

from elftools.elf.dynamic import DynamicSection
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


def main(path, show_imports="0"):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        print("arch=%s class=%d flags=%#x" % (elf["e_machine"], elf.elfclass, elf["e_flags"]))
        print("sections:", " ".join(s.name for s in elf.iter_sections() if s.name))
        for sec in elf.iter_sections():
            if isinstance(sec, DynamicSection):
                needed = [t.needed for t in sec.iter_tags() if t.entry.d_tag == "DT_NEEDED"]
                print("NEEDED:", " ".join(needed))
                soname = [t.soname for t in sec.iter_tags() if t.entry.d_tag == "DT_SONAME"]
                print("SONAME:", " ".join(soname))
        for name in (".dynsym", ".symtab"):
            sec = elf.get_section_by_name(name)
            if not isinstance(sec, SymbolTableSection):
                print(name, "absent")
                continue
            defined = [s for s in sec.iter_symbols() if s["st_shndx"] != "SHN_UNDEF" and s.name]
            undef = sorted({s.name for s in sec.iter_symbols() if s["st_shndx"] == "SHN_UNDEF" and s.name})
            funcs = [s for s in defined if s["st_info"]["type"] == "STT_FUNC"]
            thumb = sum(1 for s in funcs if s["st_value"] & 1)
            mangled = sum(1 for s in funcs if s.name.startswith("_Z"))
            jni = sorted(s.name for s in funcs if s.name.startswith("Java_"))
            print("%s: defined=%d funcs=%d thumb_funcs=%d cpp_mangled=%d undefined=%d jni=%d"
                  % (name, len(defined), len(funcs), thumb, mangled, len(undef), len(jni)))
            if name == ".dynsym":
                prefixes = Counter()
                for s in funcs:
                    n = s.name
                    if n.startswith("_ZN"):
                        i = 3
                        if n[i] == "K":
                            i += 1
                        digits = ""
                        while i < len(n) and n[i].isdigit():
                            digits += n[i]
                            i += 1
                        if digits:
                            prefixes[n[i:i + int(digits)]] += 1
                print("top C++ namespaces/classes:", ", ".join("%s(%d)" % kv for kv in prefixes.most_common(25)))
                print("JNI:", "\n  ".join([""] + jni[:80]))
                if show_imports == "1":
                    print("IMPORTS:", " ".join(undef))


if __name__ == "__main__":
    main(*sys.argv[1:])
