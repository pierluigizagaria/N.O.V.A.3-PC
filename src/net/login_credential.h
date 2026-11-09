#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace nova::net {
struct LoginCredential {
  std::string account;
  std::string token;
};

// Observe only the first HTTP response header on a verified TLS connection.
// Native login responses close that connection. Bodies are never collected,
// decoded or changed; this cannot mistake stored profile data for credentials.
class LoginCredentialHeader {
public:
  enum class Result { waiting, absent, available, invalid };
  Result feed(const void *bytes, size_t size) {
    if (done_)
      return Result::absent;
    const auto *data = static_cast<const char *>(bytes);
    for (size_t i = 0; i < size; ++i) {
      if (header_.size() == 8192) {
        done_ = true;
        return Result::invalid;
      }
      header_.push_back(data[i]);
      constexpr std::string_view prefix = "HTTP/1.";
      if (header_.size() <= prefix.size() &&
          std::string_view(header_) != prefix.substr(0, header_.size())) {
        done_ = true;
        return Result::absent;
      }
      if (header_.ends_with("\r\n\r\n")) {
        done_ = true;
        return parse();
      }
    }
    return Result::waiting;
  }
  const LoginCredential &credential() const { return credential_; }

private:
  static std::string lower(std::string_view value) {
    std::string result(value);
    for (char &c : result)
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    return result;
  }
  static bool token(std::string_view value) {
    if (value.size() != 64)
      return false;
    for (char c : value)
      if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f'))
        return false;
    return true;
  }
  Result parse() {
    const auto first = header_.find("\r\n");
    const auto status = std::string_view(header_).substr(0, first);
    if (!status.starts_with("HTTP/1.1 200 ") &&
        !status.starts_with("HTTP/1.0 200 "))
      return Result::absent;
    bool account = false, session = false;
    size_t cursor = first + 2;
    while (cursor < header_.size() - 2) {
      const auto end = header_.find("\r\n", cursor);
      auto line = std::string_view(header_).substr(cursor, end - cursor);
      const auto colon = line.find(':');
      if (colon == std::string_view::npos)
        return Result::invalid;
      const auto name = lower(line.substr(0, colon));
      auto value = line.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
      while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
      if (name == "x-nova-account") {
        if (account || !token(value))
          return Result::invalid;
        account = true;
        credential_.account = value;
      } else if (name == "x-nova-session") {
        if (session || !token(value))
          return Result::invalid;
        session = true;
        credential_.token = value;
      }
      cursor = end + 2;
    }
    if (account != session)
      return Result::invalid;
    return account ? Result::available : Result::absent;
  }
  std::string header_;
  LoginCredential credential_;
  bool done_ = false;
};
} // namespace nova::net
