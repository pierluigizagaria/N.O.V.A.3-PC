#include "abi/guest_call.h"
static_assert(sizeof(void *) == 4, "Guest boundary must be compiled for x86");

namespace nova::abi {
extern "C" __declspec(naked) uint32_t __cdecl call_guest(uint32_t, const uint32_t *, size_t) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov eax, ecx
        shl eax, 2
        sub esp, eax
        and esp, -16
        mov edi, esp
        mov esi, [ebp + 12]
        cld
        rep movsd
        call dword ptr [ebp + 8]
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
extern "C" __declspec(naked) uint64_t __cdecl call_guest_u64(uint32_t, const uint32_t *, size_t) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov eax, ecx
        shl eax, 2
        sub esp, eax
        and esp, -16
        mov edi, esp
        mov esi, [ebp + 12]
        cld
        rep movsd
        call dword ptr [ebp + 8]
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
extern "C" __declspec(naked) double __cdecl call_guest_double(uint32_t, const uint32_t *, size_t) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov eax, ecx
        shl eax, 2
        sub esp, eax
        and esp, -16
        mov edi, esp
        mov esi, [ebp + 12]
        cld
        rep movsd
        call dword ptr [ebp + 8]
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
extern "C" __declspec(naked) float __cdecl call_guest_float(uint32_t, const uint32_t *, size_t) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov eax, ecx
        shl eax, 2
        sub esp, eax
        and esp, -16
        mov edi, esp
        mov esi, [ebp + 12]
        cld
        rep movsd
        call dword ptr [ebp + 8]
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
} // namespace nova::abi
