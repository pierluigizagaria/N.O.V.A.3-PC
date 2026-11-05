#pragma once
#include "loader/elf_image.h"
namespace nova {
void report_imports(const ElfImage &image);
}
