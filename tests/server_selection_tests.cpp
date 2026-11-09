#include "net/server_config.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>

namespace fs = std::filesystem;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv) {
  try {
    check(argc == 2, "expected owned test directory");
    const auto root = fs::absolute(argv[1]) /
                      ("session-" + std::to_string(GetCurrentProcessId()) +
                       "-" + std::to_string(GetTickCount64()));
    fs::create_directories(root / "optional profile");
    struct Cleanup {
      fs::path root;
      ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); }
    } cleanup{root};
    const auto file = root / "server.conf";
    check(!nova::net::read_server_config(file).present,
          "missing file preserves legacy mode");
    const std::string common;
    auto read = [&](const std::string &text) {
      std::ofstream(file, std::ios::binary) << text;
      return nova::net::read_server_config(file);
    };
    auto reject = [&](const std::string &text) {
      bool failed = false;
      try {
        read(text);
      } catch (const std::exception &) {
        failed = true;
      }
      check(failed, "invalid configuration admitted");
    };
    auto local = read("mode = online" + common);
    check(local.address == "127.1.1.1" &&
              local.resolved_address == local.address,
          "same-machine endpoints");
    check(local.profile_root.empty(),
          "relative paths and habitual profile by default");
    auto remote = read(
        "\xef\xbb\xbf# UTF-8 BOM\r\nmode = online\r\naddress = 192.168.1.8" +
        common);
    check(remote.resolved_address == "192.168.1.8",
          "prices follow dedicated server address");
    std::map<std::string, std::string> hosts{{"untouched.invalid", "block"}};
    nova::net::apply_server_hosts(remote, hosts);
    check(hosts.at("gllive.gameloft.com") == "192.168.1.8" &&
              hosts.at("vgold.gameloft.com") == "192.168.1.8",
          "bootstrap routing");
    nova::net::apply_server_hosts(read("mode = offline"), hosts);
    check(hosts.at("gllive.gameloft.com") == "block" &&
              hosts.at("untouched.invalid") == "block",
          "offline overrides previous maps");
    const auto selected =
        read("mode=online\nprofile_root=optional profile" + common);
    check(selected.profile_root == root / "optional profile",
          "optional profile path");
    check(read("mode=offline\nprofile_root=optional profile").profile_root ==
              selected.profile_root,
          "optional offline profile");
    for (const auto *mode : {"internet", "", "ONLINE", "local", "remote"})
      reject(std::string("mode = ") + mode);
    reject("address = 127.1.1.1");
    reject("mode=online\nmode=offline" + common);
    reject("mode=online\nunknown=true" + common);
    reject("mode=online\nprofile_root=missing-profile" + common);
    // No process lifecycle fields exist in client configuration.
    reject("mode=online\nstart_local=true" + common);
    reject("mode=online\nserver_executable=program.exe" + common);
    reject("mode=online\nca_file=missing\nprice_request_file=price.txt");
    for (const auto *ip : {"192.168.01.8", "255.255.255.255", "224.1.1.1",
                           "1.2.3", "1.2.3.256", "1.2.3.4:443", "1.2.3.4/"})
      reject(std::string("mode=online\naddress=") + ip + common);
    for (const auto *ip : {"127.0.0.1", "192.168.0.8", "10.0.0.12"})
      check(read(std::string("mode=online\naddress=") + ip).resolved_address ==
                ip,
            "binary IPv4 addresses with zero octets are supported");
    auto named = read("mode=online\naddress=Server.Example.test");
    check(named.address == "server.example.test" &&
              named.resolved_address.empty(),
          "parse does not contact DNS");
    named.resolved_address = "127.1.1.1";
    nova::net::apply_server_hosts(named, hosts);
    check(!nova::net::server_ipv4(named.address) &&
              hosts.at(named.address) == "127.1.1.1",
          "hostname identity survives DNS resolution");
    check(hosts.at("confirmation.gameloft.com") == "127.1.1.1",
          "prices follow the single server");
    for (const auto *key : {"ca_file", "price_request_file", "catalogue",
                            "price_rows", "price_port", "price_address"})
      reject(std::string("mode=online\n") + key + "=old-setting");
    for (const auto *name : {"http://server.test", "-bad.test", "bad..test",
                             "bad.test.", "vgold.gameloft.com"})
      reject(std::string("mode=online\naddress=") + name);
    reject("mode=online\nprice_rows=513" + common);
    check(nova::net::server_ipv4("192.168.0.8"),
          "price transport has no native zero-octet bug");
    std::cout << "PASS dedicated server selection, optional profile, routing "
                 "and strict validation\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
