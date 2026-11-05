#pragma once
#include <cstdint>

namespace nova::elf {
struct Header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Program { uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; };
struct Section { uint32_t name, type, flags, addr, offset, size, link, info, addralign, entsize; };
struct Dynamic { int32_t tag; uint32_t value; };
struct Symbol { uint32_t name, value, size; uint8_t info, other; uint16_t shndx; };
struct Rel { uint32_t offset, info; };
static_assert(sizeof(Header) == 52 && sizeof(Program) == 32 && sizeof(Section) == 40);
static_assert(sizeof(Dynamic) == 8 && sizeof(Symbol) == 16 && sizeof(Rel) == 8);
constexpr uint32_t pt_load = 1, pt_dynamic = 2, pt_tls = 7;
constexpr uint32_t pf_x = 1, pf_w = 2, pf_r = 4;
constexpr uint32_t relative = 8, jmp_slot = 7, glob_dat = 6, absolute = 1;
}
