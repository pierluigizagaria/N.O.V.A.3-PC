#pragma once
#include <cstddef>
#include <cstdint>

namespace nova::abi {
extern "C" float __cdecl call_guest_float(uint32_t function, const uint32_t *words, size_t count);
extern "C" double __cdecl call_guest_double(uint32_t function, const uint32_t *words, size_t count);
extern "C" uint64_t __cdecl call_guest_u64(uint32_t function, const uint32_t *words, size_t count);
// Scalar/pointer cdecl calls only. The caller owns argument storage.
// The guest sees ESP % 16 == 12 immediately on entry; EAX is returned.
// The typed helpers above preserve EDX:EAX or x87 ST(0). Struct returns are unsupported.
extern "C" uint32_t __cdecl call_guest(uint32_t function, const uint32_t *arguments, size_t argument_count);
inline uint32_t call0(uint32_t function) { return call_guest(function, nullptr, 0); }
} // namespace nova::abi
