#include "net/local_shop.h"
#include "net/native_server_tls.h"
#include "bionic/runtime.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <winhttp.h>

namespace nova::net {
namespace {
constexpr size_t max_body = 65536;
bool server_configured = false;
std::optional<ServerConfig> server_shop;
uint16_t server_shop_port = 18443;
uint32_t decimal(std::string_view text, uint32_t maximum) {
  uint32_t value = 0;
  auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || (text.size() > 1 && text[0] == '0') || text[0] == '+' ||
      result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value > maximum)
    throw std::runtime_error("Invalid local shop decimal");
  return value;
}
struct Handle {
  HINTERNET value;
  explicit Handle(HINTERNET v) : value(v) {}
  ~Handle() {
    if (value)
      WinHttpCloseHandle(value);
  }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
};
bool consume(std::string_view &text, std::string_view prefix) {
  if (!text.starts_with(prefix))
    return false;
  text.remove_prefix(prefix.size());
  return true;
}
bool xml_number(std::string_view &text, std::string_view close,
                uint32_t maximum) {
  const auto at = text.find(close);
  if (at == std::string_view::npos)
    return false;
  decimal(text.substr(0, at), maximum);
  text.remove_prefix(at + close.size());
  return true;
}
} // namespace
LocalShopConfig parse_local_shop_config(const std::string &text) {
  if (text.empty() || text.size() > 8192 ||
      text.find('\0') != std::string::npos)
    throw std::runtime_error("Invalid local shop configuration size");
  std::map<std::string, std::string> fields;
  size_t start = 0;
  while (start < text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    auto line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;
    if (std::any_of(line.begin(), line.end(),
                    [](unsigned char c) { return c < 32 || c > 126; }))
      throw std::runtime_error("Invalid local shop configuration characters");
    auto equals = line.find('=');
    if (equals == std::string::npos ||
        !fields.emplace(line.substr(0, equals), line.substr(equals + 1)).second)
      throw std::runtime_error("Invalid local shop configuration fields");
  }
  const bool native_request = fields.contains("transport");
  if (native_request && fields["transport"] != "native")
    throw std::runtime_error("Unknown shop transport");
  for (const auto *key : {"origin", "request_url", "version", "price_rows"})
    if (!fields.contains(key))
      throw std::runtime_error("Missing local shop configuration field");
  if (fields.size() != 5)
    throw std::runtime_error("Unknown local shop configuration field");
  if (native_request == fields.contains("token"))
    throw std::runtime_error("Choose native transport or compatibility token");
  constexpr std::string_view prefix = "http://127.0.0.1:";
  if (!fields["origin"].starts_with(prefix))
    throw std::runtime_error("Local shop requires literal IPv4 loopback HTTP");
  auto port =
      decimal(std::string_view(fields["origin"]).substr(prefix.size()), 65535);
  if (!port)
    throw std::runtime_error("Local shop requires a nonzero destination port");
  auto version = fields["version"];
  if (version.empty() || version.size() > 64 || version[0] < 'a' ||
      version[0] > 'z' ||
      std::any_of(version.begin(), version.end(), [](char c) {
        return !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '.' &&
               c != '_' && c != '-';
      }))
    throw std::runtime_error("Invalid local shop catalogue version");
  auto token = native_request ? std::string{} : fields["token"];
  if (!native_request && (token.size() != 64 ||
                          std::any_of(token.begin(), token.end(), [](char c) {
                            return !(c >= '0' && c <= '9') &&
                                   !(c >= 'a' && c <= 'f');
                          })))
    throw std::runtime_error("Invalid local shop session token");
  const auto &request_url = fields["request_url"];
  const auto target_at = request_url.find('/', 7);
  if (!request_url.starts_with("http://") || request_url.size() > 2048 ||
      target_at == std::string::npos || target_at == 7 ||
      request_url.find_first_of("#@ ") != std::string::npos)
    throw std::runtime_error("Invalid exact legacy request identity");
  auto rows = decimal(fields["price_rows"], 512);
  if (!rows)
    throw std::runtime_error("Missing price rows");
  return {static_cast<uint16_t>(port),
          rows,
          request_url,
          version,
          token,
          native_request};
}
bool valid_shop_prices(const std::vector<uint8_t> &body, unsigned expected) {
  if (body.empty() || body.size() > max_body || expected == 0 || expected > 512)
    return false;
  std::string_view text(reinterpret_cast<const char *>(body.data()),
                        body.size());
  try {
    if (!consume(text, "<offline_item_prices><items>"))
      return false;
    unsigned rows = 0;
    while (text.starts_with("<item>")) {
      if (++rows > expected || !consume(text, "<item><price_value>") ||
          !xml_number(text, "</price_value>", 2147483647) ||
          !consume(text, "<previous_price_value>") ||
          !xml_number(text, "</previous_price_value></item>", 1000))
        return false;
    }
    return rows == expected && text == "</items></offline_item_prices>";
  } catch (const std::exception &) {
    return false;
  }
}
std::optional<std::vector<uint8_t>>
fetch_shop_prices(const LocalShopConfig &config) {
  if (config.original_response &&
      (!config.native_request || config.price_rows != native_price_rows ||
       !config.token.empty()))
    throw std::runtime_error("Invalid original price transport contract");
  if (!(server_ipv4(config.address) ||
        (config.secure && server_hostname(config.address))) ||
      (!config.native_request && config.address != "127.0.0.1"))
    throw std::runtime_error("Invalid explicit shop server address");
  // Configuration is validated again for callers outside the environment path.
  const auto checked = parse_local_shop_config(
      "origin=http://127.0.0.1:" + std::to_string(config.port) +
      "\nrequest_url=" + config.request_url + "\nversion=" + config.version +
      (config.native_request ? "\ntransport=native"
                             : "\ntoken=" + config.token) +
      "\nprice_rows=" + std::to_string(config.price_rows) + "\n");
  Handle session(
      WinHttpOpen(L"NOVA local test shop", WINHTTP_ACCESS_TYPE_NO_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session.value ||
      !WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000))
    return std::nullopt;
  if (config.secure) {
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS,
                          &protocols, sizeof(protocols)))
      return std::nullopt;
  }
  const std::wstring address(config.address.begin(), config.address.end());
  Handle connection(
      WinHttpConnect(session.value, address.c_str(), checked.port, 0));
  if (!connection.value)
    return std::nullopt;
  const auto target =
      checked.native_request
          ? checked.request_url.substr(checked.request_url.find('/', 7))
          : "/compat/v1/prices?version=" + checked.version;
  std::wstring path(target.begin(), target.end());
  Handle request(WinHttpOpenRequest(
      connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, config.secure ? WINHTTP_FLAG_SECURE : 0));
  if (!request.value)
    return std::nullopt;
  if (config.secure && server_ipv4(config.address)) {
    // Preserve explicit-IP local certificate policy. Domain selections always
    // use Windows certificate/hostname validation, without these exceptions.
    DWORD exceptions = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                       SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                       SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                       SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_SECURITY_FLAGS,
                          &exceptions, sizeof(exceptions)))
      return std::nullopt;
  }
  if (config.original_response) {
    // Preserve the original virtual host as well as the path/query. Only
    // the TCP destination is redirected to the selected dedicated server.
    const auto end = checked.request_url.find('/', 7);
    const auto authority = checked.request_url.substr(7, end - 7);
    const auto host =
        L"Host: " + std::wstring(authority.begin(), authority.end()) + L"\r\n";
    if (!WinHttpAddRequestHeaders(
            request.value, host.c_str(), static_cast<DWORD>(host.size()),
            WINHTTP_ADDREQ_FLAG_REPLACE | WINHTTP_ADDREQ_FLAG_ADD))
      return std::nullopt;
  }
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                        sizeof(policy)))
    return std::nullopt;
  const auto header =
      checked.native_request
          ? std::wstring{}
          : L"Authorization: Bearer " +
                std::wstring(checked.token.begin(), checked.token.end()) +
                L"\r\n";
  if (!WinHttpSendRequest(request.value, header.c_str(),
                          static_cast<DWORD>(header.size()),
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.value, nullptr))
    return std::nullopt;
  DWORD status = 0, size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
          WINHTTP_NO_HEADER_INDEX) ||
      status != 200)
    return std::nullopt;
  std::array<wchar_t, 128> version{};
  size = sizeof(version);
  if (!config.original_response &&
      (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CUSTOM,
                            L"X-Nova-Catalogue", version.data(), &size,
                            WINHTTP_NO_HEADER_INDEX) ||
       std::wstring(version.data()) !=
           std::wstring(checked.version.begin(), checked.version.end())))
    return std::nullopt;
  std::vector<uint8_t> bytes;
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.value, &available))
      return std::nullopt;
    if (!available)
      break;
    if (available > max_body - bytes.size())
      return std::nullopt;
    auto at = bytes.size();
    bytes.resize(at + available);
    DWORD received = 0;
    if (!WinHttpReadData(request.value, bytes.data() + at, available,
                         &received) ||
        !received)
      return std::nullopt;
    bytes.resize(at + received);
  }
  if (!valid_shop_prices(bytes, checked.price_rows))
    return std::nullopt;
  return bytes;
}
void configure_server_shop(const ServerConfig &server, uint16_t service_port) {
  if (!service_port)
    throw std::runtime_error("Invalid community service port");
  server_shop_port = service_port;
  server_shop = server.present && server.mode == ServerMode::online
                    ? std::optional(server)
                    : std::nullopt;
  server_configured = server.present;
}
std::optional<ConsumableInventory> server_consumables(int kind, const std::string &nonce) {
  if (!server_shop || kind < -1 || kind > 8 || (kind >= 0 && nonce.size() != 32)) return {};
  const auto credential = NativeServerTls::login_credential();
  if (!credential) return {};
  Handle session(WinHttpOpen(L"NOVA Community Client",WINHTTP_ACCESS_TYPE_NO_PROXY,
                             WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
  if (!session.value || !WinHttpSetTimeouts(session.value,2000,2000,2000,2000)) return {};
  const std::wstring address(server_shop->address.begin(),server_shop->address.end());
  Handle connection(WinHttpConnect(session.value,address.c_str(),server_shop_port,0));
  if (!connection.value) return {};
  Handle request(WinHttpOpenRequest(connection.value,kind < 0 ? L"GET" : L"POST",
      L"/pc/consumables",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
  if (!request.value) return {};
  if (server_ipv4(server_shop->address)) {
    DWORD exceptions=SECURITY_FLAG_IGNORE_UNKNOWN_CA|SECURITY_FLAG_IGNORE_CERT_CN_INVALID|
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID|SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    if (!WinHttpSetOption(request.value,WINHTTP_OPTION_SECURITY_FLAGS,&exceptions,sizeof(exceptions))) return {};
  }
  DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy))) return {};
  const std::wstring headers=L"Authorization: Bearer "+
      std::wstring(credential->token.begin(),credential->token.end())+L"\r\n";
  const std::string body=kind < 0 ? "" : nonce+"|"+std::to_string(kind);
  if (!WinHttpSendRequest(request.value,headers.c_str(),static_cast<DWORD>(headers.size()),
      body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
      static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0) ||
      !WinHttpReceiveResponse(request.value,nullptr)) return {};
  DWORD status=0,size=sizeof(status);
  if (!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX) || status!=200) return {};
  std::string response;
  for (;;) {
    char buffer[128]; DWORD received=0;
    if (!WinHttpReadData(request.value,buffer,sizeof(buffer),&received)) return {};
    if (!received) break;
    if (response.size()+received>256) return {};
    response.append(buffer,received);
  }
  try {
    ConsumableInventory result;
    std::array<uint32_t,11> fields{};
    std::string_view remaining(response);
    for (size_t i=0;i<fields.size();++i) {
      const auto separator=remaining.find('|');
      if ((separator==std::string_view::npos)!=(i==fields.size()-1)) return {};
      fields[i]=decimal(remaining.substr(0,separator),i==0?1:2147483647);
      if (separator!=std::string_view::npos) remaining.remove_prefix(separator+1);
    }
    result.accepted=fields[0]!=0; result.coins=fields[1];
    std::copy(fields.begin()+2,fields.end(),result.counts.begin());
    return result;
  } catch (const std::exception &) { return {}; }
}
std::optional<std::vector<uint8_t>>
local_shop_file(const std::string &url) noexcept {
  try {
    if (server_configured) {
      if (!server_shop || !url.starts_with("http://") || url.size() > 2048 ||
          std::any_of(url.begin(), url.end(),
                      [](unsigned char c) { return c <= 32 || c >= 127; }) ||
          url.find_first_of("#@\\") != std::string::npos)
        return std::nullopt;
      const auto slash = url.find('/', 7);
      if (slash == std::string::npos)
        return std::nullopt;
      const auto authority = url.substr(7, slash - 7);
      const auto colon = authority.find(':');
      auto host = authority.substr(0, colon);
      std::transform(
          host.begin(), host.end(), host.begin(),
          [](unsigned char c) { return static_cast<char>(tolower(c)); });
      if (host != "confirmation.gameloft.com")
        return std::nullopt;
      const auto port = colon == std::string::npos
                            ? 80u
                            : decimal(authority.substr(colon + 1), 65535);
      if (!port)
        return std::nullopt;
      LocalShopConfig original;
      original.port = server_shop_port;
      original.price_rows = native_price_rows;
      original.request_url = url;
      original.version =
          "native-original"; // Internal parser tag, never sent on the wire.
      original.native_request = true;
      original.address = server_shop->address;
      original.original_response = true;
      original.secure = true;
      return fetch_shop_prices(original);
    }
#if defined(NOVA_DEVELOPER_INTERFACE)
    const auto length =
        GetEnvironmentVariableW(L"NOVA_TEST_SHOP_CONFIG", nullptr, 0);
    if (!length)
      return std::nullopt;
    if (length > 32768)
      throw std::runtime_error("Invalid local shop config path");
    std::wstring path(length, L'\0');
    if (GetEnvironmentVariableW(L"NOVA_TEST_SHOP_CONFIG", path.data(),
                                length) != length - 1)
      return std::nullopt;
    path.resize(length - 1);
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input)
      throw std::runtime_error("Local shop config is unreadable");
    std::array<char, 8193> buffer{};
    input.read(buffer.data(), buffer.size());
    const auto count = input.gcount();
    const auto config = parse_local_shop_config(
        std::string(buffer.data(), static_cast<size_t>(count)));
    if (url != config.request_url)
      return std::nullopt;
    auto result = fetch_shop_prices(config);
    bionic::log_once(result ? "Local shop price response accepted"
                            : "Local shop price request failed");
    return result;
#else
    return std::nullopt;
#endif
  } catch (...) {
    // A diagnostic must not let an exception escape the Java callback.
    try {
      bionic::log_once("Local shop configuration/request rejected");
    } catch (...) {
    }
    return std::nullopt;
  }
}
} // namespace nova::net
