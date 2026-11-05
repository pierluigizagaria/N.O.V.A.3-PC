#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <process.h>
#include <vector>

namespace nova::bionic {
namespace {
// Android's pthread_once_t is one 32-bit word: 0=new, 1=running, 2=done.
// Guest callbacks always re-enter through the alignment boundary.
int __cdecl once(volatile long *control, uint32_t callback) {
    if (!control || !callback)
        return 22; // bionic EINVAL
    const long state = InterlockedCompareExchange(control, 1, 0);
    if (state == 0) {
        abi::call0(callback);
        InterlockedExchange(control, 2);
    } else {
        while (InterlockedCompareExchange(control, 2, 2) != 2)
            SwitchToThread();
    }
    return 0;
}
int __cdecl cond_init(uint32_t *control, const uint32_t *attribute) {
    if (!control)
        return 22;
    if (attribute && *attribute)
        return 95; // only default process-private conditions
    *control = 0;
    return 0;
}
int __cdecl cond_broadcast(volatile long *control) {
    if (!control)
        return 22;
    if (*control & 1)
        return 95; // process-shared conditions are not supported
    InterlockedExchangeAdd(control, 2);
    WakeByAddressAll(const_cast<long *>(control));
    return 0;
}
std::mutex keys_guard;
std::map<DWORD, uint32_t> key_destructors;
int __cdecl key_create(uint32_t *key, uint32_t destructor) {
    if (!key)
        return 22;
    const DWORD slot = TlsAlloc();
    if (slot == TLS_OUT_OF_INDEXES)
        return 11;
    try {
        std::lock_guard lock(keys_guard);
        key_destructors.emplace(slot, destructor);
        *key = slot;
        return 0;
    } catch (...) {
        TlsFree(slot);
        return 12;
    }
}
// Process-scoped keys; destructor iteration is performed by the guest thread exit path.
struct Mutex {
    explicit Mutex(uint32_t kind) : type(kind) {}
    std::mutex guard;
    std::condition_variable changed;
    DWORD owner = 0;
    uint32_t depth = 0, type;
};
// Process-scoped side table: preserve the guest's 4-byte storage and static
// initializer bits. Host synchronization objects never overwrite guest memory.
std::mutex table_guard;
std::map<uint32_t *, std::shared_ptr<Mutex>> mutexes;
std::shared_ptr<Mutex> get_mutex(uint32_t *control) {
    std::lock_guard lock(table_guard);
    auto &item = mutexes[control];
    if (!item)
        item = std::make_shared<Mutex>((*control >> 14) & 3);
    return item;
}
int __cdecl mutexattr_init(uint32_t *attribute) {
    if (!attribute)
        return 22;
    *attribute = 0;
    return 0;
}
int __cdecl mutexattr_settype(uint32_t *attribute, int type) {
    if (!attribute || type < 0 || type > 2)
        return 22;
    *attribute = (*attribute & ~15u) | static_cast<uint32_t>(type);
    return 0;
}
int __cdecl mutexattr_destroy(uint32_t *attribute) {
    if (!attribute)
        return 22;
    *attribute = UINT32_MAX;
    return 0;
}
int __cdecl mutex_init(uint32_t *control, const uint32_t *attribute) {
    if (!control)
        return 22;
    const uint32_t value = attribute ? *attribute : 0;
    if ((value & 15) > 2)
        return 22;
    if (value & 16)
        return 95; // ENOTSUP for process-shared mutexes
    try {
        std::lock_guard lock(table_guard);
        mutexes.erase(control);
        *control = (value & 15) << 14;
        return 0;
    } catch (...) {
        return 12;
    }
}
int __cdecl mutex_lock(uint32_t *control) {
    if (!control || (*control & 0x2000) || ((*control >> 14) & 3) == 3)
        return 22;
    try {
        auto mutex = get_mutex(control);
        std::unique_lock lock(mutex->guard);
        const DWORD self = GetCurrentThreadId();
        if (mutex->owner == self && mutex->type == 1) {
            if (mutex->depth == UINT32_MAX)
                return 11; // EAGAIN
            ++mutex->depth;
            return 0;
        }
        if (mutex->owner == self && mutex->type == 2)
            return 35; // EDEADLK
        mutex->changed.wait(lock, [&] { return mutex->owner == 0; });
        mutex->owner = self;
        mutex->depth = 1;
        return 0;
    } catch (...) {
        return 12;
    } // ENOMEM
}
int __cdecl mutex_unlock(uint32_t *control) {
    if (!control || (*control & 0x2000) || ((*control >> 14) & 3) == 3)
        return 22;
    try {
        auto mutex = get_mutex(control);
        std::lock_guard lock(mutex->guard);
        if (mutex->owner != GetCurrentThreadId())
            return 1; // EPERM
        if (--mutex->depth == 0) {
            mutex->owner = 0;
            mutex->changed.notify_one();
        }
        return 0;
    } catch (...) {
        return 12;
    }
}

int __cdecl mutex_trylock(uint32_t *control) {
    if (!control || (*control & 0x2000) || ((*control >> 14) & 3) == 3)
        return 22;
    auto m = get_mutex(control);
    std::lock_guard lock(m->guard);
    DWORD self = GetCurrentThreadId();
    if (m->owner == self && m->type == 1) {
        if (m->depth == UINT32_MAX)
            return 11;
        ++m->depth;
        return 0;
    }
    if (m->owner)
        return 16;
    m->owner = self;
    m->depth = 1;
    return 0;
}
int __cdecl mutex_destroy(uint32_t *control) {
    if (!control)
        return 22;
    std::lock_guard lock(table_guard);
    auto it = mutexes.find(control);
    if (it != mutexes.end()) {
        std::lock_guard state(it->second->guard);
        if (it->second->owner)
            return 16;
    }
    mutexes.erase(control);
    *control = 0xdead;
    return 0;
}
int __cdecl cond_signal(volatile long *c) {
    if (!c)
        return 22;
    if (*c & 1)
        return 95;
    InterlockedExchangeAdd(c, 2);
    WakeByAddressSingle(const_cast<long *>(c));
    return 0;
}
int __cdecl cond_destroy(uint32_t *c) {
    if (!c)
        return 22;
    *c = 0;
    return 0;
}
int condition_wait(volatile long *c, uint32_t *m, const Timespec *absolute) {
    if (!c || !m)
        return 22;
    if (*c & 1)
        return 95;
    DWORD timeout = INFINITE;
    if (absolute) {
        if (absolute->nanoseconds < 0 || absolute->nanoseconds >= 1000000000)
            return 22;
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        int64_t ticks = static_cast<int64_t>((uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime) -
                        116444736000000000LL;
        int64_t ms =
            (int64_t(absolute->seconds) * 10000000 + absolute->nanoseconds / 100 - ticks + 9999) / 10000;
        timeout = ms <= 0 ? 0 : static_cast<DWORD>(std::min<int64_t>(ms, INFINITE - 1));
    }
    long sequence = InterlockedCompareExchange(c, 0, 0);
    int rc = mutex_unlock(m);
    if (rc)
        return rc;
    BOOL woke = WaitOnAddress(c, &sequence, sizeof(sequence), timeout);
    DWORD error = GetLastError();
    rc = mutex_lock(m);
    if (rc)
        return rc;
    return woke ? 0 : error == ERROR_TIMEOUT ? 110 : 22;
}
int __cdecl cond_wait(volatile long *c, uint32_t *m) { return condition_wait(c, m, nullptr); }
int __cdecl cond_timedwait(volatile long *c, uint32_t *m, const Timespec *t) {
    if (!t)
        return 22;
    return condition_wait(c, m, t);
}
int __cdecl key_delete(uint32_t key) {
    std::lock_guard lock(keys_guard);
    if (!key_destructors.erase(key))
        return 22;
    TlsFree(key);
    return 0;
}
int __cdecl set_specific(uint32_t key, const void *value) {
    std::lock_guard lock(keys_guard);
    if (!key_destructors.contains(key))
        return 22;
    return TlsSetValue(key, const_cast<void *>(value)) ? 0 : 12;
}
void *__cdecl get_specific(uint32_t key) {
    std::lock_guard lock(keys_guard);
    return key_destructors.contains(key) ? TlsGetValue(key) : nullptr;
}
void destroy_keys() {
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<std::pair<uint32_t, uint32_t>> calls;
        {
            std::lock_guard lock(keys_guard);
            for (const auto &[key, callback] : key_destructors) {
                auto value = reinterpret_cast<uint32_t>(TlsGetValue(key));
                if (value && callback) {
                    TlsSetValue(key, nullptr);
                    calls.emplace_back(callback, value);
                }
            }
        }
        if (calls.empty())
            return;
        for (auto [callback, value] : calls)
            abi::call_guest(callback, &value, 1);
    }
}
struct Thread {
    HANDLE handle = nullptr;
    uint32_t id = 0, entry = 0, arg = 0, result = 0;
    bool detached = false, joining = false, done = false;
    ~Thread() {
        if (handle)
            CloseHandle(handle);
    }
};
std::mutex threads_guard;
std::map<uint32_t, std::shared_ptr<Thread>> threads;
uint32_t next_thread = 1;
thread_local uint32_t current_thread = 0;
std::atomic<void(__cdecl *)()> thread_cleanup{nullptr};
uint32_t __cdecl thread_self() {
    if (!current_thread) {
        std::lock_guard lock(threads_guard);
        current_thread = next_thread++;
    }
    return current_thread;
}
int __cdecl thread_equal(uint32_t a, uint32_t b) { return a == b; }
void finish_thread(uint32_t result) {
    destroy_keys();
    if (auto cleanup = thread_cleanup.load())
        cleanup();
    std::lock_guard lock(threads_guard);
    auto it = threads.find(current_thread);
    if (it != threads.end()) {
        it->second->result = result;
        it->second->done = true;
        if (it->second->detached)
            threads.erase(it);
    }
}
[[noreturn]] void __cdecl thread_exit(uint32_t value) {
    finish_thread(value);
    _endthreadex(0);
    __assume(false);
}
unsigned __stdcall thread_entry(void *raw) {
    auto *t = static_cast<Thread *>(raw);
    current_thread = t->id;
    guest_errno = 0;
    uint32_t result = abi::call_guest(t->entry, &t->arg, 1);
    finish_thread(result);
    return 0;
}
int __cdecl attr_init(ThreadAttr *a) {
    if (!a)
        return 22;
    *a = {0, 0, 1024 * 1024, 4096, 0, 0};
    return 0;
}
int __cdecl attr_destroy(ThreadAttr *a) {
    if (!a)
        return 22;
    *a = {};
    return 0;
}
int __cdecl attr_detach(ThreadAttr *a, int state) {
    if (!a || (state != 0 && state != 1))
        return 22;
    a->flags = (a->flags & ~1u) | state;
    return 0;
}
int __cdecl attr_stack(ThreadAttr *a, uint32_t size) {
    if (!a || size < 8192)
        return 22;
    a->stack_size = size;
    return 0;
}
int __cdecl thread_create(uint32_t *out, const ThreadAttr *attr, uint32_t entry, uint32_t arg) {
    if (!out || !entry)
        return 22;
    if (attr && (attr->stack_base || attr->policy || attr->priority || (attr->flags & ~1u)))
        return 95;
    if (attr && attr->stack_size < 8192)
        return 22;
    // Custom guard sizes remain unimplemented; retain the existing Windows
    // guard behavior rather than claiming an exact Linux stack layout.
    try {
        auto t = std::make_shared<Thread>();
        t->entry = entry;
        t->arg = arg;
        t->detached = attr && (attr->flags & 1);
        std::lock_guard lock(threads_guard);
        t->id = next_thread++;
        // Allocate table ownership before creating a suspended native thread:
        // allocation failure must not strand an unpublished thread or CRT data.
        auto position = threads.emplace(t->id, t).first;
        unsigned windows_id = 0;
        // This is initial commit, not only reservation: an unprobed ELF frame
        // can skip several Windows guard pages with a single ESP subtraction.
        t->handle = reinterpret_cast<HANDLE>(
            _beginthreadex(nullptr, attr ? attr->stack_size : 1024 * 1024, thread_entry,
                           t.get(), CREATE_SUSPENDED, &windows_id));
        if (!t->handle) {
            threads.erase(position);
            return 11;
        }
        *out = t->id;
        ResumeThread(t->handle);
        return 0;
    } catch (const std::bad_alloc &) {
        return 11;
    }
}
int __cdecl thread_join(uint32_t id, uint32_t *result) {
    if (id == thread_self())
        return 35;
    std::shared_ptr<Thread> t;
    {
        std::lock_guard lock(threads_guard);
        auto it = threads.find(id);
        if (it == threads.end())
            return 3;
        t = it->second;
        if (t->detached || t->joining)
            return 22;
        t->joining = true;
    }
    WaitForSingleObject(t->handle, INFINITE);
    if (result)
        *result = t->result;
    {
        std::lock_guard lock(threads_guard);
        threads.erase(id);
    }
    return 0;
}
int __cdecl thread_detach(uint32_t id) {
    std::lock_guard lock(threads_guard);
    auto it = threads.find(id);
    if (it == threads.end())
        return 3;
    auto t = it->second;
    if (t->detached || t->joining)
        return 22;
    t->detached = true;
    if (t->done)
        threads.erase(it);
    return 0;
}
int __cdecl thread_kill(uint32_t id, int signal) {
    if (signal < 0 || signal >= 64)
        return 22;
    if (id != thread_self()) {
        std::lock_guard lock(threads_guard);
        if (!threads.contains(id))
            return 3;
    }
    log_once("pthread_kill recorded signal " + std::to_string(signal));
    return 0;
}
struct Semaphore {
    std::mutex guard;
    std::condition_variable changed;
    uint32_t count = 0, waiters = 0;
};
std::mutex sem_guard;
std::map<uint32_t *, std::shared_ptr<Semaphore>> semaphores;
std::shared_ptr<Semaphore> semaphore(uint32_t *p) {
    std::lock_guard lock(sem_guard);
    auto it = semaphores.find(p);
    if (it == semaphores.end()) {
        fail(22);
        return {};
    }
    return it->second;
}
int __cdecl sem_init(uint32_t *p, int shared, uint32_t value) {
    if (!p || value > 0x3fffffff)
        return fail(22);
    if (shared)
        return fail(38);
    auto s = std::make_shared<Semaphore>();
    s->count = value;
    std::lock_guard lock(sem_guard);
    semaphores[p] = s;
    *p = value << 1;
    return 0;
}
int __cdecl sem_wait(uint32_t *p) {
    auto s = semaphore(p);
    if (!s)
        return -1;
    std::unique_lock lock(s->guard);
    ++s->waiters;
    s->changed.wait(lock, [&] { return s->count != 0; });
    --s->waiters;
    --s->count;
    *p = s->count << 1;
    return 0;
}
int __cdecl sem_trywait(uint32_t *p) {
    auto s = semaphore(p);
    if (!s)
        return -1;
    std::lock_guard lock(s->guard);
    if (!s->count)
        return fail(11);
    --s->count;
    *p = s->count << 1;
    return 0;
}
int __cdecl sem_post(uint32_t *p) {
    auto s = semaphore(p);
    if (!s)
        return -1;
    std::lock_guard lock(s->guard);
    if (s->count == 0x3fffffff)
        return fail(75);
    ++s->count;
    *p = s->count << 1;
    s->changed.notify_one();
    return 0;
}
int __cdecl sem_destroy(uint32_t *p) {
    auto s = semaphore(p);
    if (!s)
        return -1;
    std::lock_guard state(s->guard);
    if (s->waiters)
        return fail(16);
    std::lock_guard lock(sem_guard);
    semaphores.erase(p);
    *p = 0;
    return 0;
}
int __cdecl yield() {
    SwitchToThread();
    return 0;
}
int __cdecl microsleep(uint32_t n) {
    if (n >= 1000000)
        return fail(22);
    Sleep((n + 999) / 1000);
    return 0;
}
uint32_t __cdecl secondsleep(uint32_t n) {
    while (n > 4000000) {
        Sleep(4000000000u);
        n -= 4000000;
    }
    Sleep(n * 1000);
    return 0;
}
} // namespace
void set_thread_cleanup(void(__cdecl *cleanup)()) { thread_cleanup.store(cleanup); }
void enter_host_thread() { guest_errno = 0; thread_self(); }
void leave_host_thread() { finish_thread(0); current_thread = 0; }
void register_pthread(ImportRegistry &registry) {
#define B(n, f) registry.function(n, reinterpret_cast<const void *>(&f))
    B("pthread_mutex_trylock", mutex_trylock);
    B("pthread_mutex_destroy", mutex_destroy);
    B("pthread_cond_wait", cond_wait);
    B("pthread_cond_timedwait", cond_timedwait);
    B("pthread_cond_signal", cond_signal);
    B("pthread_cond_destroy", cond_destroy);
    B("pthread_getspecific", get_specific);
    B("pthread_setspecific", set_specific);
    B("pthread_key_delete", key_delete);
    B("pthread_create", thread_create);
    B("pthread_join", thread_join);
    B("pthread_detach", thread_detach);
    B("pthread_exit", thread_exit);
    B("pthread_self", thread_self);
    B("pthread_equal", thread_equal);
    B("pthread_kill", thread_kill);
    B("pthread_attr_init", attr_init);
    B("pthread_attr_destroy", attr_destroy);
    B("pthread_attr_setdetachstate", attr_detach);
    B("pthread_attr_setstacksize", attr_stack);
    B("sem_init", sem_init);
    B("sem_wait", sem_wait);
    B("sem_trywait", sem_trywait);
    B("sem_post", sem_post);
    B("sem_destroy", sem_destroy);
    B("sched_yield", yield);
    B("usleep", microsleep);
    B("sleep", secondsleep);
#undef B
    registry.function("pthread_key_create", reinterpret_cast<const void *>(&key_create));
    registry.function("pthread_cond_init", reinterpret_cast<const void *>(&cond_init));
    registry.function("pthread_mutexattr_init", reinterpret_cast<const void *>(&mutexattr_init));
    registry.function("pthread_mutexattr_settype", reinterpret_cast<const void *>(&mutexattr_settype));
    registry.function("pthread_mutexattr_destroy", reinterpret_cast<const void *>(&mutexattr_destroy));
    registry.function("pthread_mutex_init", reinterpret_cast<const void *>(&mutex_init));
    registry.function("pthread_cond_broadcast", reinterpret_cast<const void *>(&cond_broadcast));
    registry.function("pthread_once", reinterpret_cast<const void *>(&once));
    registry.function("pthread_mutex_lock", reinterpret_cast<const void *>(&mutex_lock));
    registry.function("pthread_mutex_unlock", reinterpret_cast<const void *>(&mutex_unlock));
}
} // namespace nova::bionic
