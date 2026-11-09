#include "net/local_shop.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<uint8_t> bytes(const std::string &text) { return {text.begin(), text.end()}; }
std::string config(std::string origin = "http://127.0.0.1:8085") {
    return "origin=" + origin +
           "\nrequest_url=http://legacy.invalid/exact?product=1\nversion=unit-v1\ntoken=" +
           std::string(64, 'a') + "\nprice_rows=2\n";
}
void rejected(const std::string &text) {
    bool failed = false;
    try {
        nova::net::parse_local_shop_config(text);
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "invalid config accepted");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--environment-probe") == 0) {
            nova::net::local_shop_file(argv[2]);
            return 0;
        }
        if ((argc == 4 || argc == 5) && (std::strcmp(argv[1], "--server-fetch") == 0 || std::strcmp(argv[1], "--server-offline") == 0)) {
            auto server = nova::net::read_server_config(argv[2]);
            nova::net::resolve_server_address(server);
            const auto port = argc == 5 ? std::stoul(argv[4]) : 18443;
            check(port && port <= 65535, "synthetic HTTPS port");
            nova::net::configure_server_shop(server, static_cast<uint16_t>(port));
            const auto response = nova::net::local_shop_file(argv[3]);
            check(response.has_value() == (std::strcmp(argv[1], "--server-fetch") == 0), "configured server response");
            check(!nova::net::local_shop_file("http://legacy.invalid/unrelated"), "unrelated identity must not be routed");
            return 0;
        }
        if (argc == 3 && std::strcmp(argv[1], "--fetch") == 0) {
            std::ifstream in(argv[2], std::ios::binary);
            check(bool(in), "config readable");
            std::string text((std::istreambuf_iterator<char>(in)), {});
            auto result = nova::net::fetch_shop_prices(nova::net::parse_local_shop_config(text));
            check(result.has_value(), "loopback response rejected");
            std::printf("PASS loopback price response: %zu bytes\n", result->size());
            return 0;
        }
        check(argc == 1, "usage: local-shop-tests [--fetch CONFIG]");
        auto valid = nova::net::parse_local_shop_config(config());
        check(valid.port == 8085 && valid.price_rows == 2 && valid.version == "unit-v1",
              "valid config");
        auto native_config = config();
        native_config.replace(native_config.find("token="), 70, "transport=native");
        const auto native = nova::net::parse_local_shop_config(native_config);
        check(native.native_request && native.token.empty() &&
                  native.request_url == "http://legacy.invalid/exact?product=1",
              "native destination-only price transport");
        rejected(native_config + "token=" + std::string(64, 'a') + "\n");
        for (auto origin : {"http://localhost:8085", "http://127.0.0.2:8085", "http://0.0.0.0:8085",
                            "https://127.0.0.1:8085", "http://127.0.0.1:8085/path",
                            "http://127.0.0.1:0", "http://127.0.0.1:65536"})
            rejected(config(origin));
        rejected(config() + "token=duplicate\n");
        rejected(config() + "extra=unknown\n");
        rejected(config().replace(config().find("unit-v1"), 7, "bad version"));
        auto row = "<item><price_value>12</price_value><previous_price_value>3</"
                   "previous_price_value></item>";
        auto xml = std::string("<offline_item_prices><items>") + row + row +
                   "</items></offline_item_prices>";
        check(nova::net::valid_shop_prices(bytes(xml), 2), "valid XML");
        check(!nova::net::valid_shop_prices(bytes(xml), 1), "wrong row count");
        auto missing = xml;
        auto at = missing.find("<price_value>");
        missing.erase(at, std::string("<price_value>12</price_value>").size());
        check(!nova::net::valid_shop_prices(bytes(missing), 2), "missing mandatory child rejected");
        auto negative = xml;
        negative.replace(negative.find(">12<") + 1, 2, "-1");
        check(!nova::net::valid_shop_prices(bytes(negative), 2), "negative price rejected");
        check(!nova::net::valid_shop_prices(bytes(xml + "trailing"), 2), "trailing XML rejected");
        check(!nova::net::valid_shop_prices(std::vector<uint8_t>(65537, 'x'), 2),
              "oversized body rejected");
        std::puts("PASS local shop config and XML preflight controls");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
