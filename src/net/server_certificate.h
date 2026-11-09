#pragma once
#include <Windows.h>
#include <string>
#include <wincrypt.h>

namespace nova::net {
// Offline Windows chain/name verification. The optional engine belongs to the
// caller and exists only to provide isolated trust stores in component tests.
bool verify_server_certificate(PCCERT_CONTEXT leaf, HCERTSTORE chain,
                               const std::string &hostname,
                               HCERTCHAINENGINE engine = nullptr);
} // namespace nova::net
