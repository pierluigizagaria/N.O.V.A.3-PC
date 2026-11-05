#pragma once
#include <cstdint>
namespace nova {
class ElfImage;
class Diagnostics {
public:
    explicit Diagnostics(const ElfImage& image);
    ~Diagnostics();
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
private:
    void* handler_ = nullptr;
    unsigned previous_error_mode_ = 0;
};
void print_stack(const ElfImage& image, uint32_t stack);
}
