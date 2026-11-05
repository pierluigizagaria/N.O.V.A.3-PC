#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <mutex>
namespace nova::bionic {
// The guest-visible size is independent of the Windows CRT allocator.
struct alignas(16) Allocation {
    size_t usable;
    size_t requested;
    uint32_t reserved[2];
};
void *__cdecl heap_allocate(size_t size) {
    if (size > SIZE_MAX - sizeof(Allocation) - 11) {
        fail(12);
        return nullptr;
    }
    size_t usable = size < 12 ? 12 : ((size + 11) & ~size_t(7)) - 4;
    auto *header = static_cast<Allocation *>(_aligned_malloc(sizeof(Allocation) + usable, 16));
    if (!header) {
        fail(12);
        return nullptr;
    }
    header->usable = usable;
    header->requested = size;
    void *p = header + 1;
    // Android dlmalloc compatibility: stable non-zero small-chunk slack.
    // 0xa5a5a5a5 is neither 0, 1 nor -1; see the lockPointer analysis.
    std::memset(static_cast<unsigned char *>(p) + size, 0xa5, usable - size);
    return p;
}
void __cdecl heap_release(void *p) {
    if (p)
        _aligned_free(static_cast<Allocation *>(p) - 1);
}
size_t __cdecl heap_usable_size(void *p) { return p ? (static_cast<Allocation *>(p) - 1)->usable : 0; }
namespace {
void *__cdecl reallocate(void *p, size_t size) {
    if (!p)
        return heap_allocate(size);
    if (!size) {
        heap_release(p);
        return nullptr;
    }
    void *out = heap_allocate(size);
    if (!out)
        return nullptr;
    std::memcpy(out, p, (std::min)(heap_usable_size(p), size));
    heap_release(p);
    return out;
}
void *__cdecl zero_allocate(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) {
        fail(12);
        return nullptr;
    }
    void *p = heap_allocate(n * size);
    if (p)
        std::memset(p, 0, n * size);
    return p;
}
int *__cdecl error_pointer() { return &guest_errno; }
int __cdecl compare_guest(void *context, const void *a, const void *b) {
    uint32_t words[] = {reinterpret_cast<uint32_t>(a), reinterpret_cast<uint32_t>(b)};
    return static_cast<int>(abi::call_guest(reinterpret_cast<uint32_t>(context), words, 2));
}
void __cdecl sort(void *p, size_t n, size_t size, uint32_t comparator) {
    if (!size || n < 2)
        return;
    if (n > SIZE_MAX / size || !comparator) {
        fail(22);
        return;
    }
    qsort_s(p, n, size, compare_guest, reinterpret_cast<void *>(comparator));
}
std::mutex random_guard;
uint64_t random_state = 0x1234abcd330eULL;
void __cdecl seed48(int32_t seed) {
    std::lock_guard lock(random_guard);
    random_state = (uint64_t(static_cast<uint32_t>(seed)) << 16) | 0x330e;
}
int32_t __cdecl random48() {
    std::lock_guard lock(random_guard);
    random_state = (random_state * 0x5deece66dULL + 11) & 0xffffffffffffULL;
    return static_cast<int32_t>(random_state >> 17);
}
} // namespace
void register_stdlib(ImportRegistry &r) {
#define B(n, f) r.function(n, reinterpret_cast<const void *>(&f))
    B("malloc", heap_allocate);
    B("malloc_usable_size", heap_usable_size);
    B("free", heap_release);
    B("realloc", reallocate);
    B("calloc", zero_allocate);
    B("__errno", error_pointer);
    B("qsort", sort);
    B("lrand48", random48);
    B("srand48", seed48);
#undef B
}
} // namespace nova::bionic
