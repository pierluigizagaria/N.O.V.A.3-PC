#include "net/server_certificate.h"

namespace nova::net {
bool verify_server_certificate(PCCERT_CONTEXT leaf, HCERTSTORE store,
                               const std::string &hostname,
                               HCERTCHAINENGINE engine) {
  if (!leaf || hostname.empty())
    return false;
  CERT_CHAIN_PARA parameters{};
  parameters.cbSize = sizeof(parameters);
  LPSTR usage = const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);
  parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
  parameters.RequestedUsage.Usage.cUsageIdentifier = 1;
  parameters.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
  PCCERT_CHAIN_CONTEXT chain = nullptr;
  // No AIA/root downloads: network traffic stays on the selected server.
  if (!CertGetCertificateChain(engine, leaf, nullptr, store, &parameters,
                               CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL |
                                   CERT_CHAIN_DISABLE_AIA,
                               nullptr, &chain))
    return false;
  struct Release {
    PCCERT_CHAIN_CONTEXT p;
    ~Release() { CertFreeCertificateChain(p); }
  } release{chain};
  std::wstring name(hostname.begin(), hostname.end());
  SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
  ssl.cbSize = sizeof(ssl);
  ssl.dwAuthType = AUTHTYPE_SERVER;
  ssl.pwszServerName = name.data();
  CERT_CHAIN_POLICY_PARA policy{};
  policy.cbSize = sizeof(policy);
  policy.pvExtraPolicyPara = &ssl;
  CERT_CHAIN_POLICY_STATUS status{};
  status.cbSize = sizeof(status);
  return CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy,
                                          &status) &&
         status.dwError == 0;
}
} // namespace nova::net
