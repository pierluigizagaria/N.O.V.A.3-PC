#include "net/network.h"
#include "net/interfaces.h"
#include "net/community_lobby.h"
#include "net/community_match.h"
#include "platform/profile.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace nova::net {
using bionic::Descriptor;
using bionic::fail;
namespace {
std::shared_ptr<Descriptor> socket_descriptor(int fd) {
    auto d = bionic::descriptor(fd);
    if (d && !d->is_socket()) {
        fail(88);
        return {};
    }
    return d;
}
int result(int value) { return value == SOCKET_ERROR ? fail(socket_error(WSAGetLastError())) : value; }
int io_result(int value) {
    if (value != SOCKET_ERROR)
        return value;
    int error = WSAGetLastError();
    // Linux SO_RCVTIMEO/SO_SNDTIMEO report EAGAIN, not connect's ETIMEDOUT.
    return fail(error == WSAETIMEDOUT ? 11 : socket_error(error));
}
int family_to_host(int family) { return family == 2 ? AF_INET : family == 10 ? AF_INET6 : -1; }
int message_flags(int flags) {
    if (flags & ~(1 | 2 | 4 | 0x100 | 0x4000)) {
        bionic::log_once("unsupported socket message flags " + std::to_string(flags));
        return fail(95);
    }
    return (flags & 1 ? MSG_OOB : 0) | (flags & 2 ? MSG_PEEK : 0) | (flags & 4 ? MSG_DONTROUTE : 0) |
           (flags & 0x100 ? MSG_WAITALL : 0);
}
std::string endpoint(const sockaddr *address, int size) {
    if (!address)
        return "-";
    char text[INET6_ADDRSTRLEN]{};
    if (address->sa_family == AF_INET && size >= static_cast<int>(sizeof(sockaddr_in))) {
        auto p = reinterpret_cast<const sockaddr_in *>(address);
        InetNtopA(AF_INET, const_cast<in_addr *>(&p->sin_addr), text, sizeof text);
        return std::string(text) + ":" + std::to_string(ntohs(p->sin_port));
    }
    if (address->sa_family == AF_INET6 && size >= static_cast<int>(sizeof(sockaddr_in6))) {
        auto p = reinterpret_cast<const sockaddr_in6 *>(address);
        InetNtopA(AF_INET6, const_cast<in6_addr *>(&p->sin6_addr), text, sizeof text);
        return "[" + std::string(text) + "]:" + std::to_string(ntohs(p->sin6_port));
    }
    return "-";
}
int __cdecl create_socket(int domain, int type, int protocol) {
    initialize();
    int family = family_to_host(domain);
    if (family < 0)
        return fail(97);
    int base = type & ~(0x800 | 0x80000);
    if (base != SOCK_STREAM && base != SOCK_DGRAM)
        return fail(94);
    if (protocol && protocol != (base == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP))
        return fail(93);
    SOCKET s = ::socket(family, base, protocol);
    if (s == INVALID_SOCKET)
        return fail(socket_error(WSAGetLastError()));
    auto d = std::make_shared<Descriptor>();
    d->socket = s;
    d->domain = domain;
    d->type = base;
    d->flags = 2 | (type & (0x800 | 0x80000));
    if ((type & 0x800) && nonblocking(d, true) < 0)
        return -1;
    return bionic::adopt_descriptor(d);
}
int __cdecl bind_socket(int fd, const void *address, uint32_t size) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    sockaddr_storage host{};
    int length = 0;
    if (!to_host_address(address, size, host, length))
        return -1;
    return result(::bind(d->socket, reinterpret_cast<sockaddr *>(&host), length));
}
int __cdecl listen_socket(int fd, int backlog) {
    auto d = socket_descriptor(fd);
    return d ? result(::listen(d->socket, backlog)) : -1;
}
int __cdecl accept_socket(int fd, void *address, uint32_t *size) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    if (address && !size)
        return fail(14);
    sockaddr_storage host{};
    int length = sizeof host;
    SOCKET s = ::accept(d->socket, reinterpret_cast<sockaddr *>(&host), &length);
    if (s == INVALID_SOCKET)
        return fail(socket_error(WSAGetLastError()));
    auto accepted = std::make_shared<Descriptor>();
    accepted->socket = s;
    accepted->domain = d->domain;
    accepted->type = d->type;
    accepted->flags = 2;
    // Linux accept does not inherit O_NONBLOCK (Winsock does).
    if (nonblocking(accepted, false) < 0)
        return -1;
    int out = bionic::adopt_descriptor(accepted);
    if (out >= 0 && address)
        from_host_address(reinterpret_cast<sockaddr *>(&host), length, address, size);
    return out;
}
int __cdecl connect_socket(int fd, const void *address, uint32_t size) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    sockaddr_storage host{};
    int length = 0;
    if (!to_host_address(address, size, host, length))
        return -1;
    try {
        CommunityLobby::attach(d,reinterpret_cast<sockaddr *>(&host),length);
    } catch (...) { return fail(12); }
    int rc = ::connect(d->socket, reinterpret_cast<sockaddr *>(&host), length);
    if (rc != SOCKET_ERROR)
        return rc;
    int e = WSAGetLastError();
    return fail(e == WSAEWOULDBLOCK ? 115 : socket_error(e));
}
int __cdecl send_socket(int fd, const void *bytes, uint32_t size, int flags) {
    auto d = socket_descriptor(fd);
    return d ? write_socket(d, bytes, size, flags) : -1;
}
int __cdecl recv_socket(int fd, void *bytes, uint32_t size, int flags) {
    auto d = socket_descriptor(fd);
    return d ? read_socket(d, bytes, size, flags) : -1;
}
int __cdecl send_to(int fd, const void *bytes, uint32_t size, int flags, const void *address,
                    uint32_t length) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    if (!address)
        return write_socket(d, bytes, size, flags);
    if (size > INT32_MAX || (!bytes && size))
        return fail(!bytes ? 14 : 22);
    int f = message_flags(flags);
    if (f < 0)
        return -1;
    sockaddr_storage host{};
    int host_length = 0;
    if (!to_host_address(address, length, host, host_length))
        return -1;
    if (auto wrapped = CommunityMatch::send(d, bytes, size, flags,
            reinterpret_cast<sockaddr *>(&host), host_length))
        return *wrapped;
    int n = io_result(::sendto(d->socket, static_cast<const char *>(bytes), static_cast<int>(size), f,
                               reinterpret_cast<sockaddr *>(&host), host_length));
    if (n >= 0)
        traffic(*d, "send", bytes, n, reinterpret_cast<sockaddr *>(&host), host_length);
    return n;
}
int __cdecl receive_from(int fd, void *bytes, uint32_t size, int flags, void *address, uint32_t *length) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    if (address && !length)
        return fail(14);
    if (size > INT32_MAX || (!bytes && size))
        return fail(!bytes ? 14 : 22);
    int f = message_flags(flags);
    if (f < 0)
        return -1;
    sockaddr_storage host{};
    int host_length = sizeof host;
    if (auto wrapped = CommunityMatch::receive(d, bytes, size, flags, host, host_length)) {
        if (*wrapped >= 0 && address)
            from_host_address(reinterpret_cast<sockaddr *>(&host), host_length, address, length);
        return *wrapped;
    }
    int n = ::recvfrom(d->socket, static_cast<char *>(bytes), static_cast<int>(size), f,
                       reinterpret_cast<sockaddr *>(&host), &host_length);
    // Linux returns the copied prefix for a short UDP receive, without EMSGSIZE.
    if (n == SOCKET_ERROR && WSAGetLastError() == WSAEMSGSIZE && d->type == SOCK_DGRAM)
        n = static_cast<int>(size);
    else
        n = io_result(n);
    if (n >= 0) {
        if (address)
            from_host_address(reinterpret_cast<sockaddr *>(&host), host_length, address, length);
        traffic(*d, "recv", bytes, n, reinterpret_cast<sockaddr *>(&host), host_length);
    }
    return n;
}
int __cdecl shutdown_socket(int fd, int how) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    if (how < 0 || how > 2)
        return fail(22);
    return result(::shutdown(d->socket, how));
}
int socket_name(int fd, void *address, uint32_t *length, bool peer) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    if (!address || !length)
        return fail(14);
    sockaddr_storage host{};
    int size = sizeof host;
    int rc = peer ? ::getpeername(d->socket, reinterpret_cast<sockaddr *>(&host), &size)
                  : ::getsockname(d->socket, reinterpret_cast<sockaddr *>(&host), &size);
    if (rc == SOCKET_ERROR)
        return result(rc);
    from_host_address(reinterpret_cast<sockaddr *>(&host), size, address, length);
    return 0;
}
int __cdecl get_name(int fd, void *address, uint32_t *length) {
    return socket_name(fd, address, length, false);
}
int __cdecl get_peer(int fd, void *address, uint32_t *length) {
    return socket_name(fd, address, length, true);
}
int option(int level, int name, int &host_level) {
    if (level == 1) {
        host_level = SOL_SOCKET;
        switch (name) {
        case 1:
            return SO_DEBUG;
        case 2:
            return SO_REUSEADDR;
        case 3:
            return SO_TYPE;
        case 4:
            return SO_ERROR;
        case 5:
            return SO_DONTROUTE;
        case 6:
            return SO_BROADCAST;
        case 7:
            return SO_SNDBUF;
        case 8:
            return SO_RCVBUF;
        case 9:
            return SO_KEEPALIVE;
        case 10:
            return SO_OOBINLINE;
        case 13:
            return SO_LINGER;
        case 20:
            return SO_RCVTIMEO;
        case 21:
            return SO_SNDTIMEO;
        case 30:
            return SO_ACCEPTCONN;
        }
    } else if (level == 6 && name == 1) {
        host_level = IPPROTO_TCP;
        return TCP_NODELAY;
    } else if (level == 0) {
        host_level = IPPROTO_IP;
        switch (name) {
        case 1:
            return IP_TOS;
        case 2:
            return IP_TTL;
        case 32:
            return IP_MULTICAST_IF;
        case 33:
            return IP_MULTICAST_TTL;
        case 34:
            return IP_MULTICAST_LOOP;
        case 35:
            return IP_ADD_MEMBERSHIP;
        case 36:
            return IP_DROP_MEMBERSHIP;
        }
    } else if (level == 41 && name == 26) {
        host_level = IPPROTO_IPV6;
        return IPV6_V6ONLY;
    }
    bionic::log_once("unsupported socket option " + std::to_string(level) + ":" + std::to_string(name));
    return fail(92);
}
int __cdecl set_option(int fd, int level, int name, const void *value, uint32_t length) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    if (!value)
        return fail(14);
    int host_level = 0, host_name = option(level, name, host_level);
    if (host_name < 0)
        return -1;
    linger linger_value{};
    DWORD timeout = 0;
    int integer = 0;
    if (level == 1 && name == 13) {
        if (length < 8)
            return fail(22);
        auto v = static_cast<const int *>(value);
        if (v[1] < 0 || v[1] > 65535)
            return fail(22);
        linger_value.l_onoff = v[0] ? 1 : 0;
        linger_value.l_linger = static_cast<u_short>(v[1]);
        value = &linger_value;
        length = sizeof linger_value;
    } else if (level == 1 && (name == 20 || name == 21)) {
        if (length < sizeof(Timeval))
            return fail(22);
        auto t = static_cast<const Timeval *>(value);
        if (t->seconds < 0 || t->micros < 0 || t->micros >= 1000000)
            return fail(33);
        uint64_t ms = uint64_t(t->seconds) * 1000 + (t->micros + 999) / 1000;
        if (ms > MAXDWORD)
            return fail(22);
        timeout = static_cast<DWORD>(ms);
        value = &timeout;
        length = sizeof timeout;
    } else if (level == 0 && (name == 33 || name == 34)) {
        if (!length)
            return fail(22);
        integer =
            length >= 4 ? *static_cast<const int *>(value) : *static_cast<const unsigned char *>(value);
        value = &integer;
        length = 4;
    } else if (length > INT32_MAX)
        return fail(22);
    else if (!(level == 0 && (name == 32 || name == 35 || name == 36)) && length < 4)
        return fail(22);
    return result(::setsockopt(d->socket, host_level, host_name, static_cast<const char *>(value),
                               static_cast<int>(length)));
}
int __cdecl get_option(int fd, int level, int name, void *value, uint32_t *length) {
    auto d = socket_descriptor(fd);
    if (!d)
        return -1;
    if (!value || !length)
        return fail(14);
    int host_level = 0, host_name = option(level, name, host_level);
    if (host_name < 0)
        return -1;
    unsigned char storage[128]{};
    int size = sizeof storage;
    if (::getsockopt(d->socket, host_level, host_name, reinterpret_cast<char *>(storage), &size) ==
        SOCKET_ERROR)
        return result(SOCKET_ERROR);
    unsigned char converted[128]{};
    int out_size = size;
    if (level == 1 && name == 13) {
        auto v = reinterpret_cast<linger *>(storage);
        int pair[2] = {v->l_onoff, v->l_linger};
        std::memcpy(converted, pair, 8);
        out_size = 8;
    } else if (level == 1 && (name == 20 || name == 21)) {
        DWORD ms = 0;
        std::memcpy(&ms, storage, 4);
        Timeval t{static_cast<int32_t>(ms / 1000), static_cast<int32_t>((ms % 1000) * 1000)};
        std::memcpy(converted, &t, sizeof t);
        out_size = sizeof t;
    } else {
        std::memcpy(converted, storage, size);
        if (level == 1 && name == 4) {
            int e;
            std::memcpy(&e, storage, 4);
            e = socket_error(e);
            std::memcpy(converted, &e, 4);
        }
    }
    uint32_t copied = (std::min)(*length, static_cast<uint32_t>(out_size));
    std::memcpy(value, converted, copied);
    *length = copied;
    return 0;
}
int __cdecl poll_sockets(PollFd *fds, uint32_t count, int timeout) {
    if (count > 32768 || (!fds && count))
        return fail(!fds ? 14 : 22);
    std::vector<std::shared_ptr<Descriptor>> held(count);
    std::vector<WSAPOLLFD> sockets;
    std::vector<uint32_t> indices;
    std::vector<std::optional<CommunityLobby::PollPlan>> plans;
    bool buffered = false;
    int saved_errno = bionic::guest_errno;
    int ready = 0;
    for (uint32_t i = 0; i < count; ++i) {
        auto &p = fds[i];
        p.returned = 0;
        if (p.fd < 0)
            continue;
        auto d = bionic::descriptor(p.fd);
        held[i] = d;
        if (!d) {
            p.returned = 32;
            ++ready;
            continue;
        }
        if (d->is_socket()) {
            short events = 0;
            if (p.events & (1 | 64))
                events |= POLLRDNORM;
            // Microsoft Winsock rejects POLLPRI; use select for OOB below.
            if (p.events & (4 | 256))
                events |= POLLWRNORM;
            auto plan=CommunityLobby::poll(d,events);
            if (events & POLLRDNORM) {
                if (auto native = CommunityLobby::readable(d)) {
                    if (*native != 0) buffered = true;
                    if (plan) plan->immediate &= ~POLLRDNORM;
                }
                if (auto native = CommunityMatch::readable(d)) {
                    if (*native == 1) buffered = true;
                    if (*native == -2) events &= ~POLLRDNORM;
                }
            }
            if(plan && plan->immediate)buffered=true;
            sockets.push_back({d->socket, plan?plan->events:events, 0});
            plans.push_back(plan);
            indices.push_back(i);
        } else {
            // Regular files are always ready, including EOF, as on Linux.
            p.returned = p.events & (1 | 4 | 64 | 256);
            if (p.returned)
                ++ready;
        }
    }
    bionic::guest_errno = saved_errno;
    auto deadline = GetTickCount64() + (timeout < 0 ? 0 : static_cast<uint64_t>(timeout));
    for (;;) {
        int wait = (ready || buffered) ? 0
                   : timeout < 0
                       ? 50
                       : static_cast<int>((std::min)(uint64_t(50), deadline > GetTickCount64()
                                                                       ? deadline - GetTickCount64()
                                                                       : 0));
        if (sockets.empty()) {
            if (!ready && wait)
                Sleep(wait);
        } else {
            int rc = WSAPoll(sockets.data(), static_cast<ULONG>(sockets.size()), wait);
            if (rc == SOCKET_ERROR)
                return result(rc);
            std::vector<bool> priority(sockets.size(), false);
            // Winsock select supports OOB readiness even when WSAPoll does not.
            // Batch to stay inside the counted Winsock fd_set capacity.
            for (size_t first = 0; first < sockets.size(); first += FD_SETSIZE) {
                fd_set exceptions;
                FD_ZERO(&exceptions);
                size_t last = (std::min)(sockets.size(), first + FD_SETSIZE);
                for (size_t j = first; j < last; ++j)
                    if (fds[indices[j]].events & (2 | 128))
                        FD_SET(sockets[j].fd, &exceptions);
                if (exceptions.fd_count) {
                    timeval immediate{};
                    if (::select(0, nullptr, nullptr, &exceptions, &immediate) == SOCKET_ERROR)
                        return result(SOCKET_ERROR);
                    for (size_t j = first; j < last; ++j)
                        priority[j] = FD_ISSET(sockets[j].fd, &exceptions) != 0;
                }
            }
            for (size_t j = 0; j < sockets.size(); ++j) {
                auto bits = sockets[j].revents;
                if(plans[j]) {
                    bits|=plans[j]->immediate;
                    if(plans[j]->retry) {
                        const bool retry_ready=(bits&plans[j]->retry)!=0;
                        // TLS handshake traffic wakes the pending writer, not
                        // native readers waiting for an HTTP/lobby response.
                        bits&=~(POLLRDNORM|POLLWRNORM);
                        if(retry_ready)bits|=POLLWRNORM;
                    }
                }
                auto &p = fds[indices[j]];
                if (p.events & (1 | 64)) {
                    if (auto native = CommunityLobby::readable(held[indices[j]])) {
                        bits &= ~POLLRDNORM;
                        if (*native == 1) bits |= POLLRDNORM;
                        else if (*native < 0) bits |= POLLERR;
                    }
                    if (auto native = CommunityMatch::readable(held[indices[j]])) {
                        bits &= ~POLLRDNORM;
                        if (*native == 1) bits |= POLLRDNORM;
                        else if (*native == -2) sockets[j].events &= ~POLLRDNORM;
                        else if (*native < 0) bits |= POLLERR;
                    }
                }
                short translated = 0;
                if (bits & POLLRDNORM)
                    translated |= p.events & (1 | 64);
                if ((bits & (POLLPRI | POLLRDBAND)) || (priority[j] && !(bits & (POLLERR | POLLHUP))))
                    translated |= p.events & (2 | 128);
                if (bits & POLLWRNORM)
                    translated |= p.events & (4 | 256);
                if (bits & POLLERR)
                    translated |= 8;
                // Winsock reports only HUP on EOF; Linux also makes reads ready.
                if (bits & POLLHUP)
                    translated |= 16 | (p.events & (1 | 64));
                if (bits & POLLNVAL)
                    translated |= 32;
                p.returned = translated;
                if (translated)
                    ++ready;
            }
        }
        if (ready || (timeout >= 0 && GetTickCount64() >= deadline))
            return ready;
    }
}
int __cdecl select_sockets(int count, uint32_t *read, uint32_t *write, uint32_t *except, Timeval *time) {
    if (count < 0 || count > 1024)
        return fail(22);
    if (time && (time->seconds < 0 || time->micros < 0 || time->micros >= 1000000))
        return fail(22);
    uint64_t ms = time ? uint64_t(time->seconds) * 1000 + (time->micros + 999) / 1000 : 0;
    if (ms > INT32_MAX)
        return fail(22);
    std::vector<PollFd> poll;
    for (int i = 0; i < count; ++i) {
        short events = 0;
        if (read && (read[i / 32] & (1u << (i % 32))))
            events |= 1;
        if (write && (write[i / 32] & (1u << (i % 32))))
            events |= 4;
        if (except && (except[i / 32] & (1u << (i % 32))))
            events |= 2;
        if (events) {
            if (!bionic::descriptor(i))
                return -1;
            poll.push_back({i, events, 0});
        }
    }
    auto start = GetTickCount64();
    int rc =
        poll_sockets(poll.data(), static_cast<uint32_t>(poll.size()), time ? static_cast<int>(ms) : -1);
    if (rc < 0)
        return -1;
    size_t bytes = static_cast<size_t>((count + 31) / 32) * 4;
    if (read)
        std::memset(read, 0, bytes);
    if (write)
        std::memset(write, 0, bytes);
    if (except)
        std::memset(except, 0, bytes);
    int result_count = 0;
    for (const auto &p : poll) {
        if (p.returned & 32)
            return fail(9);
        auto mark = [&](uint32_t *set) {
            set[p.fd / 32] |= 1u << (p.fd % 32);
            ++result_count;
        };
        if (read && (p.events & 1) && (p.returned & (1 | 8 | 16)))
            mark(read);
        if (write && (p.events & 4) && (p.returned & (4 | 8 | 16)))
            mark(write);
        if (except && (p.events & 2) && (p.returned & 2))
            mark(except);
    }
    if (time) {
        auto elapsed = GetTickCount64() - start;
        auto remaining = ms > elapsed ? ms - elapsed : 0;
        time->seconds = static_cast<int32_t>(remaining / 1000);
        time->micros = static_cast<int32_t>((remaining % 1000) * 1000);
    }
    return result_count;
}
} // namespace
void initialize() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("Winsock initialization failed");
    });
}
int socket_error(int e) {
    switch (e) {
    case 0:
        return 0;
    case WSAEINTR:
        return 4;
    case WSAEBADF:
        return 9;
    case WSAEACCES:
        return 13;
    case WSAEFAULT:
        return 14;
    case WSAEINVAL:
        return 22;
    case WSAEMFILE:
        return 24;
    case WSAEWOULDBLOCK:
        return 11;
    case WSAEINPROGRESS:
        return 115;
    case WSAEALREADY:
        return 114;
    case WSAENOTSOCK:
        return 88;
    case WSAEDESTADDRREQ:
        return 89;
    case WSAEMSGSIZE:
        return 90;
    case WSAEPROTOTYPE:
        return 91;
    case WSAENOPROTOOPT:
        return 92;
    case WSAEPROTONOSUPPORT:
        return 93;
    case WSAESOCKTNOSUPPORT:
        return 94;
    case WSAEOPNOTSUPP:
        return 95;
    case WSAEPFNOSUPPORT:
        return 96;
    case WSAEAFNOSUPPORT:
        return 97;
    case WSAEADDRINUSE:
        return 98;
    case WSAEADDRNOTAVAIL:
        return 99;
    case WSAENETDOWN:
        return 100;
    case WSAENETUNREACH:
        return 101;
    case WSAENETRESET:
        return 102;
    case WSAECONNABORTED:
        return 103;
    case WSAECONNRESET:
        return 104;
    case WSAENOBUFS:
        return 105;
    case WSAEISCONN:
        return 106;
    case WSAENOTCONN:
        return 107;
    case WSAESHUTDOWN:
        return 32;
    case WSAETIMEDOUT:
        return 110;
    case WSAECONNREFUSED:
        return 111;
    case WSAEHOSTDOWN:
        return 112;
    case WSAEHOSTUNREACH:
        return 113;
    case WSAENAMETOOLONG:
        return 36;
    case WSA_NOT_ENOUGH_MEMORY:
        return 12;
    default:
        bionic::log_once("unmapped Winsock error " + std::to_string(e));
        return 5;
    }
}
bool to_host_address(const void *address, uint32_t size, sockaddr_storage &host, int &length) {
    if (!address) {
        fail(14);
        return false;
    }
    if (size < 2) {
        fail(22);
        return false;
    }
    uint16_t family;
    std::memcpy(&family, address, 2);
    length = family == 2 ? sizeof(sockaddr_in) : family == 10 ? sizeof(sockaddr_in6) : 0;
    if (!length) {
        fail(97);
        return false;
    }
    if (size < static_cast<uint32_t>(length)) {
        fail(22);
        return false;
    }
    std::memcpy(&host, address, length);
    host.ss_family = static_cast<ADDRESS_FAMILY>(family_to_host(family));
    return true;
}
void from_host_address(const sockaddr *host, int length, void *address, uint32_t *size) {
    Address converted{};
    auto n = static_cast<uint32_t>((std::min)(length, static_cast<int>(sizeof converted)));
    std::memcpy(&converted, host, n);
    converted.family = host->sa_family == AF_INET6 ? 10 : host->sa_family;
    std::memcpy(address, &converted, (std::min)(*size, n));
    *size = n;
}
int nonblocking(const std::shared_ptr<Descriptor> &d, bool enabled) {
    u_long value = enabled ? 1 : 0;
    if (::ioctlsocket(d->socket, FIONBIO, &value) == SOCKET_ERROR)
        return result(SOCKET_ERROR);
    if (enabled)
        d->flags |= 2048;
    else
        d->flags &= ~2048;
    return 0;
}
int socket_ioctl(const std::shared_ptr<Descriptor> &d, uint32_t request, void *p) {
    std::lock_guard lock(d->guard);
    if (request == siocgifconf || request == siocgifflags)
        return interface_ioctl(request, p);
    if (request == 0x5421)
        return nonblocking(d, *static_cast<int *>(p) != 0);
    if (request == 0x541b) {
        u_long value = 0;
        int rc = result(::ioctlsocket(d->socket, FIONREAD, &value));
        if (rc == 0)
            *static_cast<uint32_t *>(p) = value;
        return rc;
    }
    return fail(25);
}
int read_socket(const std::shared_ptr<Descriptor> &d, void *bytes, uint32_t size, int flags) {
    if (size > INT32_MAX || (!bytes && size))
        return fail(!bytes ? 14 : 22);
    int f = message_flags(flags);
    if (f < 0)
        return -1;
    if(auto secured=CommunityLobby::read(d,bytes,size,flags))return *secured;
    if (d->type == SOCK_DGRAM) {
        sockaddr_storage peer{};
        int length = sizeof(peer);
        if (auto secured = CommunityMatch::receive(d, bytes, size, flags, peer, length))
            return *secured;
    }
    int n = ::recv(d->socket, static_cast<char *>(bytes), static_cast<int>(size), f);
    if (n == SOCKET_ERROR && WSAGetLastError() == WSAEMSGSIZE && d->type == SOCK_DGRAM)
        n = static_cast<int>(size);
    else
        n = io_result(n);
    if (n >= 0)
        traffic(*d, "recv", bytes, n);
    return n;
}
int write_socket(const std::shared_ptr<Descriptor> &d, const void *bytes, uint32_t size, int flags) {
    if (size > INT32_MAX || (!bytes && size))
        return fail(!bytes ? 14 : 22);
    int f = message_flags(flags);
    if (f < 0)
        return -1;
    if(auto secured=CommunityLobby::write(d,bytes,size,flags))return *secured;
    if (d->type == SOCK_DGRAM) {
        sockaddr_storage peer{};
        int length = sizeof(peer);
        const bool connected = ::getpeername(d->socket, reinterpret_cast<sockaddr *>(&peer), &length) == 0;
        if (auto secured = CommunityMatch::send(d, bytes, size, flags,
                connected ? reinterpret_cast<sockaddr *>(&peer) : nullptr, connected ? length : 0))
            return *secured;
    }
    int n = io_result(::send(d->socket, static_cast<const char *>(bytes), static_cast<int>(size), f));
    if (n >= 0)
        traffic(*d, "send", bytes, n);
    return n;
}
void traffic(const Descriptor &d, const char *direction, const void *bytes, size_t size,
             const sockaddr *peer, int peer_length) {
    if (!profile::config().net_log)
        return;
    static std::mutex guard;
    std::lock_guard lock(guard);
    sockaddr_storage local{}, remote{};
    int local_length = sizeof local, remote_length = sizeof remote;
    ::getsockname(d.socket, reinterpret_cast<sockaddr *>(&local), &local_length);
    if (!peer && ::getpeername(d.socket, reinterpret_cast<sockaddr *>(&remote), &remote_length) == 0) {
        peer = reinterpret_cast<sockaddr *>(&remote);
        peer_length = remote_length;
    }
    auto dir = bionic::profile_directory() / "logs";
    std::filesystem::create_directories(dir);
    auto path =
        dir / ("socket-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(d.serial) + ".log");
    std::ofstream log(path, std::ios::app);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    log << ms << " socket=" << d.serial
        << " local=" << endpoint(reinterpret_cast<sockaddr *>(&local), local_length)
        << " peer=" << endpoint(peer, peer_length) << " " << direction << " length=" << size << " hex=";
    auto p = static_cast<const unsigned char *>(bytes);
    for (size_t i = 0; i < size; ++i)
        log << std::hex << std::setw(2) << std::setfill('0') << unsigned(p[i]);
    log << '\n';
    log.flush();
    if (!log)
        throw std::runtime_error("Cannot write network traffic log");
}
void register_network(ImportRegistry &r) {
#define B(n, f) r.function(n, reinterpret_cast<const void *>(&f))
    B("socket", create_socket);
    B("bind", bind_socket);
    B("listen", listen_socket);
    B("accept", accept_socket);
    B("connect", connect_socket);
    B("send", send_socket);
    B("recv", recv_socket);
    B("sendto", send_to);
    B("recvfrom", receive_from);
    B("shutdown", shutdown_socket);
    B("setsockopt", set_option);
    B("getsockopt", get_option);
    B("getsockname", get_name);
    B("getpeername", get_peer);
    B("select", select_sockets);
    B("poll", poll_sockets);
#undef B
    register_resolver(r);
}
} // namespace nova::net
