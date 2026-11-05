#include "loader/elf_image.h"
#include "abi/guest_call.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace nova {
namespace {
std::string hex(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("ELF: " + message); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }
bool range(uint64_t start, uint64_t size, uint64_t limit) {
    return start <= limit && size <= limit - start;
}
template<class T> T read_file(const std::vector<uint8_t>& file, uint64_t offset) {
    require(range(offset, sizeof(T), file.size()), "truncated file at " + hex(static_cast<uint32_t>(offset)));
    T value;
    std::memcpy(&value, file.data() + static_cast<size_t>(offset), sizeof(value));
    return value;
}
template<class T> T read_memory(uint32_t address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
std::string string_at(const char* strings, uint32_t size, uint32_t offset) {
    require(offset < size, "string offset out of bounds");
    const char* start = strings + offset;
    const void* end = std::memchr(start, 0, size - offset);
    require(end != nullptr, "unterminated string");
    return {start, static_cast<const char*>(end)};
}
}

ElfImage::ElfImage(const std::filesystem::path& path, const ImportRegistry& registry) {
    try { load(path, registry); }
    catch (...) {
        traps_.reset();
        if (memory_) VirtualFree(memory_, 0, MEM_RELEASE);
        memory_ = nullptr;
        throw;
    }
}
ElfImage::~ElfImage() {
    traps_.reset();
    if (memory_) VirtualFree(memory_, 0, MEM_RELEASE);
}

uint32_t ElfImage::address(uint32_t vaddr, size_t size) const {
    for (const auto& segment : segments_)
        if (vaddr >= segment.vaddr && range(vaddr - segment.vaddr, size, segment.memsz))
            return bias_ + vaddr;
    fail("unmapped virtual range " + hex(vaddr) + " size " + hex(static_cast<uint32_t>(size)));
}

void ElfImage::load(const std::filesystem::path& path, const ImportRegistry& registry) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(stream.good(), "cannot open " + path.string());
    const auto length = stream.tellg();
    require(length >= static_cast<std::streamoff>(sizeof(elf::Header)) &&
            length <= 512 * 1024 * 1024, "invalid file size");
    file_.resize(static_cast<size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(file_.data()), static_cast<std::streamsize>(file_.size()));
    require(stream.good(), "could not read complete file");
    const auto header = read_file<elf::Header>(file_, 0);
    require(std::memcmp(header.ident, "\177ELF", 4) == 0 &&
            header.ident[4] == 1 && header.ident[5] == 1 && header.ident[6] == 1,
            "expected little-endian ELF32");
    require(header.type == 3 && header.machine == 3 && header.version == 1,
            "expected ET_DYN / EM_386");
    require(header.ehsize == sizeof(elf::Header) && header.phentsize == sizeof(elf::Program) &&
            header.phnum > 0, "invalid program header sizes");
    require(range(header.phoff, uint64_t(header.phnum) * header.phentsize, file_.size()),
            "truncated program headers");
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    page_size_ = system.dwPageSize;
    uint64_t low = UINT32_MAX, high = 0;
    elf::Program dynamic{};
    for (uint32_t i = 0; i < header.phnum; ++i) {
        const auto p = read_file<elf::Program>(file_, uint64_t(header.phoff) + i * sizeof(elf::Program));
        require(p.type != elf::pt_tls, "PT_TLS is not supported");
        if (p.type == elf::pt_dynamic) {
            require(dynamic.memsz == 0, "multiple PT_DYNAMIC segments");
            dynamic = p;
        }
        if (p.type != elf::pt_load || p.memsz == 0) continue;
        require(p.filesz <= p.memsz && range(p.offset, p.filesz, file_.size()), "invalid PT_LOAD file range");
        require(uint64_t(p.vaddr) + p.memsz <= UINT32_MAX, "PT_LOAD address overflow");
        require(p.align <= 1 || ((p.align & (p.align - 1)) == 0 &&
                (p.vaddr % p.align) == (p.offset % p.align)), "invalid PT_LOAD alignment");
        for (const auto& previous : segments_)
            require(uint64_t(p.vaddr) + p.memsz <= previous.vaddr ||
                    uint64_t(previous.vaddr) + previous.memsz <= p.vaddr, "overlapping PT_LOAD ranges");
        low = std::min(low, uint64_t(p.vaddr & ~(page_size_ - 1)));
        high = std::max(high, (uint64_t(p.vaddr) + p.memsz + page_size_ - 1) & ~uint64_t(page_size_ - 1));
        segments_.push_back({p.vaddr, p.filesz, p.memsz, p.flags});
    }
    require(!segments_.empty() && high > low && high - low <= 512 * 1024 * 1024,
            "invalid or excessive load span");
    minimum_ = static_cast<uint32_t>(low);
    span_ = static_cast<uint32_t>(high - low);
    memory_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, span_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    require(memory_ != nullptr, "VirtualAlloc failed: " + std::to_string(GetLastError()));
    bias_ = reinterpret_cast<uint32_t>(memory_) - minimum_;
    for (uint32_t i = 0; i < header.phnum; ++i) {
        const auto p = read_file<elf::Program>(file_, uint64_t(header.phoff) + i * sizeof(elf::Program));
        if (p.type == elf::pt_load && p.memsz)
            std::memcpy(reinterpret_cast<void*>(address(p.vaddr, p.memsz)), file_.data() + p.offset, p.filesz);
    }
    require(dynamic.memsz >= sizeof(elf::Dynamic) && dynamic.memsz % sizeof(elf::Dynamic) == 0,
            "missing or invalid PT_DYNAMIC");
    address(dynamic.vaddr, dynamic.memsz);
    std::map<int32_t, uint32_t> tags;
    bool terminated = false;
    for (uint32_t offset = 0; offset < dynamic.memsz; offset += sizeof(elf::Dynamic)) {
        const auto d = read_memory<elf::Dynamic>(address(dynamic.vaddr + offset, sizeof(elf::Dynamic)));
        if (d.tag == 0) { terminated = true; break; }
        if (d.tag != 1) require(tags.emplace(d.tag, d.value).second, "duplicate dynamic tag " + std::to_string(d.tag));
    }
    require(terminated, "unterminated PT_DYNAMIC");
    auto tag = [&](int32_t key) -> uint32_t {
        const auto it = tags.find(key);
        return it == tags.end() ? 0 : it->second;
    };
    require(!tag(7) && !tag(8), "DT_RELA is not supported");
    require(tag(4) && tag(5) && tag(6) && tag(10), "DT_HASH/STRTAB/SYMTAB/STRSZ required");
    require(tag(11) == sizeof(elf::Symbol), "invalid DT_SYMENT");
    const uint32_t hash = address(tag(4), 8);
    const uint32_t buckets = read_memory<uint32_t>(hash), count = read_memory<uint32_t>(hash + 4);
    require(count > 0 && uint64_t(buckets) + count + 2 <= UINT32_MAX / 4, "invalid DT_HASH counts");
    address(tag(4), (size_t(buckets) + count + 2) * 4);
    require(count <= UINT32_MAX / sizeof(elf::Symbol), "dynamic symbol count overflow");
    const uint32_t symtab = address(tag(6), count * sizeof(elf::Symbol));
    const auto* strtab = reinterpret_cast<const char*>(address(tag(5), tag(10)));
    std::vector<uint32_t> resolved(count, 0);
    traps_ = std::make_unique<TrapStubs>(*this);
    for (uint32_t i = 0; i < count; ++i) {
        const auto s = read_memory<elf::Symbol>(symtab + i * sizeof(elf::Symbol));
        const auto name = string_at(strtab, tag(10), s.name);
        const auto type = static_cast<uint8_t>(s.info & 15);
        require(type != 6 && type != 10, "TLS/IFUNC symbols are not supported: " + name);
        if (s.shndx != 0) {
            require(s.shndx < 0xff00 || s.shndx == 0xfff1, "unsupported symbol section: " + name);
            resolved[i] = s.shndx == 0xfff1 ? s.value : address(s.value, s.size);
            if (!name.empty() && (s.info >> 4) != 0 && (s.other & 3) != 2 && (s.other & 3) != 1)
                exports_.push_back({name, s.value, s.size, type, s.shndx == 0xfff1});
        } else if (i && !name.empty()) {
            const Binding* binding = registry.find(name);
            const ImportKind kind = type == 1 ? ImportKind::data : ImportKind::function;
            if (binding) {
                require(type == 0 || binding->kind == kind, "import kind mismatch: " + name);
                resolved[i] = binding->address;
                imports_.push_back({name, resolved[i], binding->kind, false, binding->placeholder});
            } else {
                require(kind != ImportKind::data, "unregistered data import: " + name);
                resolved[i] = traps_->add(name);
                imports_.push_back({name, resolved[i], kind, true, false});
            }
        }
    }
    auto relocate = [&](uint32_t table, uint32_t bytes) {
        if (!bytes) return;
        require(table && bytes % sizeof(elf::Rel) == 0, "invalid relocation table");
        address(table, bytes);
        for (uint32_t offset = 0; offset < bytes; offset += sizeof(elf::Rel)) {
            const auto r = read_memory<elf::Rel>(address(table + offset, sizeof(elf::Rel)));
            const uint32_t type = r.info & 255, symbol = r.info >> 8;
            if (type != elf::relative && type != elf::jmp_slot && type != elf::glob_dat && type != elf::absolute)
                fail("unsupported relocation type " + std::to_string(type) + " at offset " + hex(r.offset));
            require(symbol < count, "relocation symbol index out of bounds");
            require(type != elf::relative || symbol == 0, "R_386_RELATIVE has a symbol");
            const uint32_t target = address(r.offset, 4);
            const uint32_t addend = read_memory<uint32_t>(target);
            const uint32_t value = type == elf::relative ? bias_ + addend :
                                   type == elf::absolute ? resolved[symbol] + addend : resolved[symbol];
            std::memcpy(reinterpret_cast<void*>(target), &value, 4);
            ++relocations_[type];
        }
    };
    require(!tag(18) || tag(19) == sizeof(elf::Rel), "invalid DT_RELENT");
    require(!tag(2) || tag(20) == 17, "DT_PLTREL must be DT_REL");
    if (tag(18) && tag(2))
        require(uint64_t(tag(17)) + tag(18) <= tag(23) ||
                uint64_t(tag(23)) + tag(2) <= tag(17), "overlapping relocation tables");
    relocate(tag(17), tag(18));
    relocate(tag(23), tag(2));
    init_ = tag(12);
    init_array_ = tag(25);
    require(tag(27) % 4 == 0, "invalid DT_INIT_ARRAYSZ");
    init_count_ = tag(27) / 4;
    if (init_count_) address(init_array_, init_count_ * 4);
    if (init_) require(executable(address(init_)), "DT_INIT is not executable");

    // Full file symbols are optional: stripped files still have dynamic exports.
    if (header.shnum) {
        require(header.shentsize == sizeof(elf::Section) &&
                range(header.shoff, uint64_t(header.shnum) * header.shentsize, file_.size()),
                "invalid section headers");
        for (uint32_t i = 0; i < header.shnum; ++i) {
            const auto s = read_file<elf::Section>(file_, uint64_t(header.shoff) + i * sizeof(elf::Section));
            if (s.type != 2) continue; // SHT_SYMTAB
            require(s.entsize == sizeof(elf::Symbol) && s.size % s.entsize == 0 &&
                    s.link < header.shnum && range(s.offset, s.size, file_.size()), "invalid .symtab");
            const auto strings = read_file<elf::Section>(file_, uint64_t(header.shoff) + s.link * sizeof(elf::Section));
            require(strings.type == 3 && range(strings.offset, strings.size, file_.size()), "invalid symbol strings");
            for (uint32_t offset = 0; offset < s.size; offset += sizeof(elf::Symbol)) {
                const auto symbol = read_file<elf::Symbol>(file_, uint64_t(s.offset) + offset);
                const auto type = static_cast<uint8_t>(symbol.info & 15);
                if (!symbol.shndx || symbol.shndx >= 0xff00 || (type != 2 && type != 1)) continue;
                const auto name = string_at(reinterpret_cast<const char*>(file_.data() + strings.offset),
                                            strings.size, symbol.name);
                if (!name.empty() && contains(bias_ + symbol.value))
                    symbols_.push_back({name, symbol.value, symbol.size, type});
            }
        }
    }
    if (symbols_.empty())
        for (const auto& symbol : exports_) if (!symbol.absolute) symbols_.push_back(symbol);
    std::sort(symbols_.begin(), symbols_.end(), [](const auto& a, const auto& b) {
        if (a.value != b.value) return a.value < b.value;
        return a.name < b.name;
    });
    traps_->seal();
    protect();
}

void ElfImage::protect() {
    std::vector<uint32_t> flags(span_ / page_size_, 0);
    for (const auto& s : segments_) {
        const uint32_t first = (s.vaddr - minimum_) / page_size_;
        const uint32_t end = (s.vaddr - minimum_ + s.memsz + page_size_ - 1) / page_size_;
        for (uint32_t i = first; i < end; ++i) flags[i] |= s.flags;
    }
    for (size_t i = 0; i < flags.size();) {
        const auto f = flags[i];
        require((f & (elf::pf_w | elf::pf_x)) != (elf::pf_w | elf::pf_x),
                "writable/executable page at " + hex(minimum_ + static_cast<uint32_t>(i) * page_size_));
        const DWORD protection = f & elf::pf_x ? PAGE_EXECUTE_READ :
                                 f & elf::pf_w ? PAGE_READWRITE :
                                 f & elf::pf_r ? PAGE_READONLY : PAGE_NOACCESS;
        size_t end = i + 1;
        while (end < flags.size() && flags[end] == f) ++end;
        DWORD previous;
        require(VirtualProtect(memory_ + i * page_size_, (end - i) * page_size_, protection, &previous) != 0,
                "VirtualProtect failed: " + std::to_string(GetLastError()));
        i = end;
    }
    require(FlushInstructionCache(GetCurrentProcess(), memory_, span_) != 0, "FlushInstructionCache failed");
}

uint32_t ElfImage::lookup(const std::string& name) const {
    for (const auto& symbol : exports_)
        if (symbol.name == name) return symbol.absolute ? symbol.value : bias_ + symbol.value;
    return 0;
}
uint32_t ElfImage::symbol(const std::string& name) const {
    for (const auto& symbol : symbols_)
        if (symbol.name == name) return symbol.absolute ? symbol.value : bias_ + symbol.value;
    return lookup(name);
}
bool ElfImage::contains(uint32_t ptr) const {
    for (const auto& s : segments_)
        if (ptr >= bias_ + s.vaddr && uint64_t(ptr) < uint64_t(bias_ + s.vaddr) + s.memsz) return true;
    return false;
}
bool ElfImage::executable(uint32_t ptr) const {
    for (const auto& s : segments_)
        if ((s.flags & elf::pf_x) && ptr >= bias_ + s.vaddr &&
            uint64_t(ptr) < uint64_t(bias_ + s.vaddr) + s.memsz) return true;
    return false;
}
std::string ElfImage::symbolize(uint32_t ptr) const {
    if (!contains(ptr)) return hex(ptr) + " (outside guest)";
    const uint32_t relative = ptr - bias_;
    const auto it = std::upper_bound(symbols_.begin(), symbols_.end(), relative,
                                    [](uint32_t value, const NamedSymbol& s) { return value < s.value; });
    if (it != symbols_.begin()) {
        const auto& s = *std::prev(it);
        if (!s.size || relative - s.value < s.size)
            return s.name + "+" + hex(relative - s.value) + " [guest+" + hex(relative) + "]";
    }
    return "guest+" + hex(relative);
}
size_t ElfImage::run_initializers() {
    require(init_state_ == InitState::ready, "initializers may only be run once");
    init_state_ = InitState::running;
    size_t called = 0;
    auto invoke = [&](uint32_t function, const std::string& label) {
        require(executable(function), "initializer points outside executable guest memory: " + hex(function));
        std::printf("%s %s\n", label.c_str(), symbolize(function).c_str());
        std::fflush(stdout);
        abi::call0(function);
        ++called;
    };
    if (init_) invoke(address(init_), "DT_INIT");
    for (uint32_t i = 0; i < init_count_; ++i) {
        const uint32_t function = read_memory<uint32_t>(address(init_array_ + i * 4, 4));
        if (!function || function == UINT32_MAX) continue;
        invoke(function, "init_array[" + std::to_string(i) + "]");
    }
    init_state_ = InitState::done;
    std::printf("initializers completed: %zu (array slots: %u)\n", called, init_count_);
    return called;
}
}
