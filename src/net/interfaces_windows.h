#pragma once
#include "net/interfaces.h"
#include "net/network.h"
#include <iphlpapi.h>
#include <netioapi.h>
namespace nova::net {
struct WindowsInterfaceApi {
    decltype(&GetAdaptersAddresses) addresses;
    decltype(&GetIfEntry2) entry;
};
// Host-only seam. Synthetic tests supply SDK-shaped fabricated data here;
// the production formatter/dispatch never receive Windows pointers or layouts.
int read_windows_interfaces(InterfaceSnapshot &, const WindowsInterfaceApi &);
} // namespace nova::net
