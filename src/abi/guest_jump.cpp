#include "bionic/runtime.h"
namespace nova::bionic {
// AOSP android-2.3.7_r1 arch-x86: ten words; pc, ebx, esp, ebp, esi,
// edi, signal mask, three reserved words. Signals are record-only in T02.
extern "C" __declspec(naked) int __cdecl guest_setjmp(uint32_t *) {
    __asm {
        mov edx, [esp+4]
        mov eax, [esp]
        mov [edx], eax
        mov [edx+4], ebx
        mov [edx+8], esp
        mov [edx+12], ebp
        mov [edx+16], esi
        mov [edx+20], edi
        mov dword ptr [edx+24], 0
        xor eax, eax
        ret
    }
}
extern "C" __declspec(naked) void __cdecl guest_longjmp(uint32_t *, int) {
    __asm {
        mov edx, [esp+4]
        mov eax, [esp+8]
        test eax, eax
        jnz nonzero
        inc eax
    nonzero:
        mov ecx, [edx]
        mov ebx, [edx+4]
        mov ebp, [edx+12]
        mov esi, [edx+16]
        mov edi, [edx+20]
        mov esp, [edx+8]
        mov [esp], ecx
        ret
    }
}
} // namespace nova::bionic
