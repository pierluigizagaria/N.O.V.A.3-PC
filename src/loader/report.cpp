#include "loader/report.h"
#include <cstdio>
#include <set>
namespace nova {
void report_imports(const ElfImage &image) {
    size_t implemented = 0, placeholders = 0, trapped = 0, gl = 0, sl = 0, sockets = 0, other = 0;
    static const std::set<std::string> network = {
        "accept",      "bind",        "connect",    "freeaddrinfo", "getaddrinfo", "gethostbyname",
        "gethostname", "getsockname", "getsockopt", "inet_addr",    "inet_ntoa",   "listen",
        "poll",        "recv",        "recvfrom",   "select",       "send",        "sendto",
        "setsockopt",  "shutdown",    "socket"};
    for (const auto &i : image.imports()) {
        std::printf("import %-48s 0x%08x %s %s\n", i.name.c_str(), i.address,
                    i.kind == ImportKind::data ? "data" : "function",
                    i.trapped       ? "trapped"
                    : i.placeholder ? "placeholder"
                                    : "implemented");
        if (i.trapped) {
            ++trapped;
            if (i.name.starts_with("gl"))
                ++gl;
            else if (i.name.starts_with("sl"))
                ++sl;
            else if (network.contains(i.name))
                ++sockets;
            else
                ++other;
        } else if (i.placeholder)
            ++placeholders;
        else
            ++implemented;
    }
    std::printf("imports=%zu implemented=%zu placeholders=%zu trapped=%zu\ntrapped_groups GL=%zu OpenSL=%zu "
                "sockets=%zu other=%zu\n",
                image.imports().size(), implemented, placeholders, trapped, gl, sl, sockets, other);
}
} // namespace nova
