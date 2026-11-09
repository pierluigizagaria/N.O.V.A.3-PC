#pragma once
#include "loader/elf_image.h"
#include <initializer_list>
#include <string>
#include <vector>

namespace nova::abi {
// All host-to-guest calls, including trampolines, use the aligned bridge.
uint32_t require_function(const ElfImage &image, const std::string &name);
uint32_t invoke(uint32_t function, std::initializer_list<uint32_t> arguments = {});
bool invoke_bool(uint32_t function, std::initializer_list<uint32_t> arguments = {});
// Install/remove only while guest execution is quiescent. A deliberately small
// prologue decoder rejects relocatable/relative instructions instead of guessing.
class GuestHook {
  public:
    GuestHook(const ElfImage &image, const std::string &name,
              std::initializer_list<uint8_t> expected, uint32_t replacement);
    ~GuestHook();
    GuestHook(const GuestHook &) = delete;
    GuestHook &operator=(const GuestHook &) = delete;
    uint32_t original() const { return trampoline_; }
  private:
    uint32_t target_ = 0, trampoline_ = 0;
    std::vector<uint8_t> saved_;
};
// Redirect one checked direct CALL, leaving every other caller of the callee
// untouched. Installation/removal has the same quiescence rule as GuestHook.
class GuestCall {
  public:
    GuestCall(const ElfImage &image, const std::string &owner, uint32_t offset,
              const std::string &callee, uint32_t replacement);
    ~GuestCall();
    GuestCall(const GuestCall &) = delete;
    GuestCall &operator=(const GuestCall &) = delete;
    uint32_t original() const { return original_; }
  private:
    uint32_t target_ = 0, original_ = 0;
    uint8_t saved_[5]{};
};
// Enter a host scope at a checked position-independent MOV checkpoint. Preserve
// flags, general registers, x87 and SSE state before replaying the instruction.
class GuestCheckpoint {
public:
    GuestCheckpoint(const ElfImage&,const std::string& owner,uint32_t offset,
                    std::initializer_list<uint8_t> expected,uint32_t callback);
    ~GuestCheckpoint();
    GuestCheckpoint(const GuestCheckpoint&)=delete;
private:
    uint32_t target_=0,bridge_=0;
    std::vector<uint8_t> saved_;
};
}
