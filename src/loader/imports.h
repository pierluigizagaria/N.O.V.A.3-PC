#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nova {
class ElfImage;
enum class ImportKind { function, data };
struct Binding { uint32_t address; ImportKind kind; bool placeholder; };
struct ImportStatus {
    std::string name;
    uint32_t address;
    ImportKind kind;
    bool trapped;
    bool placeholder;
};
class ImportRegistry {
public:
    void function(const std::string& name, const void* address);
    void data(const std::string& name, const void* address, bool placeholder = false);
    const Binding* find(const std::string& name) const;
private:
    std::map<std::string, Binding> entries_;
};
class TrapStubs {
public:
    explicit TrapStubs(const ElfImage& image);
    ~TrapStubs();
    TrapStubs(const TrapStubs&) = delete;
    TrapStubs& operator=(const TrapStubs&) = delete;
    uint32_t add(const std::string& name);
    void seal();
private:
    struct Entry;
    const ElfImage& image_;
    std::vector<Entry*> entries_;
    uint8_t* code_ = nullptr;
    size_t capacity_ = 0;
    bool sealed_ = false;
};
}
