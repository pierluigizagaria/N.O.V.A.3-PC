#pragma once
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include "bionic/runtime.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <winsock2.h>
namespace nova::bionic {
struct Descriptor {
    HANDLE handle = INVALID_HANDLE_VALUE;
    SOCKET socket = INVALID_SOCKET;
    std::atomic<int> flags{0};
    int domain = 0, type = 0;
    uint64_t serial = 0;
    std::mutex guard;
    bool owned = true;
    ~Descriptor();
    bool is_socket() const { return socket != INVALID_SOCKET; }
};
std::shared_ptr<Descriptor> descriptor(int fd);
int adopt_descriptor(std::shared_ptr<Descriptor> d);
} // namespace nova::bionic
