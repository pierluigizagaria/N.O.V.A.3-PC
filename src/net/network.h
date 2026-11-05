#pragma once
#include "bionic/descriptors.h"
#include <ws2tcpip.h>
namespace nova::net {
#pragma pack(push, 4)
struct Address {
    uint16_t family;
    unsigned char data[126];
};
struct AddressInfo {
    int32_t flags, family, type, protocol;
    uint32_t length;
    char *canonical;
    Address *address;
    AddressInfo *next;
};
struct HostEntry {
    char *name;
    char **aliases;
    int32_t family, length;
    char **addresses;
};
struct Timeval {
    int32_t seconds, micros;
};
struct PollFd {
    int32_t fd;
    int16_t events, returned;
};
#pragma pack(pop)
static_assert(sizeof(AddressInfo) == 32 && offsetof(AddressInfo, canonical) == 20 &&
              offsetof(AddressInfo, address) == 24);
static_assert(sizeof(HostEntry) == 20 && sizeof(PollFd) == 8);
void initialize();
int socket_error(int error);
void register_network(ImportRegistry &);
void register_resolver(ImportRegistry &);
int read_socket(const std::shared_ptr<bionic::Descriptor> &, void *, uint32_t, int flags = 0);
int write_socket(const std::shared_ptr<bionic::Descriptor> &, const void *, uint32_t, int flags = 0);
int nonblocking(const std::shared_ptr<bionic::Descriptor> &, bool);
int socket_ioctl(const std::shared_ptr<bionic::Descriptor> &, uint32_t, void *);
void traffic(const bionic::Descriptor &, const char *, const void *, size_t, const sockaddr *peer = nullptr,
             int peer_length = 0);
bool to_host_address(const void *, uint32_t, sockaddr_storage &, int &);
void from_host_address(const sockaddr *, int, void *, uint32_t *);
} // namespace nova::net
