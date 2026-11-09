#pragma once
#include "loader/elf_image.h"
#include <memory>

namespace nova::net {
class NativeConsumables {
public:
    NativeConsumables(const ElfImage&,bool (*dedicated)());
    ~NativeConsumables();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
