#pragma once
#include "loader/elf_types.h"
#include "loader/imports.h"
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nova {
struct Segment { uint32_t vaddr, filesz, memsz, flags; };
struct NamedSymbol { std::string name; uint32_t value, size; uint8_t type; bool absolute = false; };
class ElfImage {
public:
    ElfImage(const std::filesystem::path& path, const ImportRegistry& registry);
    ~ElfImage();
    ElfImage(const ElfImage&) = delete;
    ElfImage& operator=(const ElfImage&) = delete;
    uint32_t base() const { return bias_; }
    uint32_t reservation() const { return reinterpret_cast<uint32_t>(memory_); }
    uint32_t mapped_size() const { return span_; }
    // Original source bytes retained unchanged after load; valid for this image's
    // lifetime. Identity checks must use these bytes, not reopen an unrelated path.
    std::span<const uint8_t> source_bytes() const { return file_; }
    uint32_t lookup(const std::string& name) const;
    // Includes local symbols retained in the supplied ELF symbol table.
    uint32_t symbol(const std::string& name) const;
    std::string symbolize(uint32_t address) const;
    bool contains(uint32_t address) const;
    bool executable(uint32_t address) const;
    size_t run_initializers();
    const std::vector<Segment>& segments() const { return segments_; }
    const std::vector<ImportStatus>& imports() const { return imports_; }
    const std::map<uint32_t, size_t>& relocation_counts() const { return relocations_; }
    const std::vector<NamedSymbol>& exports() const { return exports_; }
    size_t initializer_count() const { return init_count_ + (init_ ? 1 : 0); }
    size_t symbol_count() const { return symbols_.size(); }
    uint32_t address(uint32_t vaddr, size_t size = 1) const;
private:
    void load(const std::filesystem::path& path, const ImportRegistry& registry);
    void protect();
    std::vector<uint8_t> file_;
    uint8_t* memory_ = nullptr;
    uint32_t bias_ = 0, minimum_ = 0, span_ = 0, page_size_ = 4096;
    uint32_t init_ = 0, init_array_ = 0, init_count_ = 0;
    enum class InitState { ready, running, done };
    InitState init_state_ = InitState::ready;
    std::vector<Segment> segments_;
    std::vector<ImportStatus> imports_;
    std::vector<NamedSymbol> exports_, symbols_;
    std::map<uint32_t, size_t> relocations_;
    std::unique_ptr<TrapStubs> traps_;
};
}
