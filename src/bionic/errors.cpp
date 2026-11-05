#include "bionic/runtime.h"
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <set>
namespace nova::bionic {
thread_local int guest_errno = 0;
int fail(int error) {
    guest_errno = error;
    return -1;
}
int win_error(DWORD e) {
    switch (e) {
    case ERROR_SUCCESS:
        return 0;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
        return 2;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        return 13;
    case ERROR_INVALID_HANDLE:
        return 9;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return 12;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return 17;
    case ERROR_DISK_FULL:
        return 28;
    case ERROR_DIR_NOT_EMPTY:
        return 39;
    case ERROR_DIRECTORY:
        return 20;
    case ERROR_BROKEN_PIPE:
        return 32;
    case ERROR_WRITE_PROTECT:
        return 30;
    case ERROR_TOO_MANY_OPEN_FILES:
        return 24;
    case ERROR_INVALID_PARAMETER:
    case ERROR_NEGATIVE_SEEK:
        return 22;
    case ERROR_NOT_SUPPORTED:
        return 95;
    case ERROR_OPERATION_ABORTED:
        return 4;
    default:
        return 5;
    }
}
int crt_error(int e) {
    switch (e) {
    case EILSEQ:
        return 84;
    case ERANGE:
        return 34;
    case EDOM:
        return 33;
    case EDEADLK:
        return 35;
    case ENAMETOOLONG:
        return 36;
    case ENOSYS:
        return 38;
    case ENOTEMPTY:
        return 39;
    default:
        return e > 0 && e <= 34 ? e : 5;
    }
}
void log_once(const std::string &message) {
    static std::mutex guard;
    static std::set<std::string> seen;
    std::lock_guard lock(guard);
    if (seen.insert(message).second)
        std::fprintf(stderr, "bionic: %s\n", message.c_str());
}
} // namespace nova::bionic
