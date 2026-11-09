#include "net/native_trust.h"
#include "loader/sha256.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace nova::net {
NativeTrust *NativeTrust::active_ = nullptr;

NativeTrust::NativeTrust(const ElfImage &image, const std::filesystem::path &path) {
    if (path.empty())
        return;
    if (active_)
        throw std::runtime_error("Native TLS trust already configured");
    if (sha256(image.source_bytes()) !=
        "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e")
        throw std::runtime_error("Native TLS trust requires the supported Android library");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read the explicit local CA certificate");
    std::array<char, 32769> buffer{};
    input.read(buffer.data(), buffer.size());
    const auto size = input.gcount();
    certificate_.assign(buffer.data(), static_cast<size_t>(size));
    if (size == 0 || size > 32768 || certificate_.find('\0') != std::string::npos ||
        !certificate_.starts_with("-----BEGIN CERTIFICATE-----") ||
        certificate_.find("-----END CERTIFICATE-----") == std::string::npos)
        throw std::runtime_error("Invalid explicit local CA PEM");
    bio_new_ = abi::require_function(image, "BIO_new_mem_buf");
    bio_free_ = abi::require_function(image, "BIO_free");
    pem_read_ = abi::require_function(image, "PEM_read_bio_X509");
    add_cert_ = abi::require_function(image, "X509_STORE_add_cert");
    free_cert_ = abi::require_function(image, "X509_free");
    verify_ = abi::require_function(image, "SSL_set_verify");
    active_ = this;
    try {
        hook_ = std::make_unique<abi::GuestHook>(
            image, "X509_STORE_new",
            std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x83, 0xec, 0x18},
            reinterpret_cast<uint32_t>(&create_store));
        original_ = hook_->original();
        attach_hook_ = std::make_unique<abi::GuestHook>(
            image, "SSL_set_fd", std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x83, 0xec, 0x38},
            reinterpret_cast<uint32_t>(&attach_socket));
        attach_original_ = attach_hook_->original();
    } catch (...) {
        active_ = nullptr;
        throw;
    }
    std::fprintf(stderr, "native local CA configured; original TLS verification retained\n");
}

NativeTrust::~NativeTrust() {
    attach_hook_.reset();
    hook_.reset();
    if (active_ == this)
        active_ = nullptr;
}

uint32_t __cdecl NativeTrust::create_store() noexcept {
    uint32_t store = 0, bio = 0, certificate = 0;
    auto *self = active_;
    if (!self)
        return 0;
    try {
        store = abi::invoke(self->original_);
        if (!store)
            return 0;
        bio = abi::invoke(self->bio_new_, {reinterpret_cast<uint32_t>(self->certificate_.data()),
                                           static_cast<uint32_t>(self->certificate_.size())});
        if (bio)
            certificate = abi::invoke(self->pem_read_, {bio, 0, 0, 0});
        const bool added = certificate && abi::invoke(self->add_cert_, {store, certificate}) == 1;
        if (certificate)
            abi::invoke(self->free_cert_, {certificate});
        certificate = 0;
        if (bio)
            abi::invoke(self->bio_free_, {bio});
        bio = 0;
        std::fprintf(stderr, "native local CA store addition=%s\n",
                     added ? "accepted" : "rejected");
    } catch (...) {
        // Exceptions cannot unwind across original guest frames. No success or
        // verification result is fabricated; the original store remains authoritative.
        if (certificate)
            try {
                abi::invoke(self->free_cert_, {certificate});
            } catch (...) {
            }
        if (bio)
            try {
                abi::invoke(self->bio_free_, {bio});
            } catch (...) {
            }
        std::fputs("native local CA addition failed\n", stderr);
    }
    return store;
}

uint32_t __cdecl NativeTrust::attach_socket(uint32_t ssl, uint32_t descriptor) noexcept {
    auto *self = active_;
    if (!self)
        return 0;
    try {
        const auto result = abi::invoke(self->attach_original_, {ssl, descriptor});
        if (result == 1)
            abi::invoke(self->verify_, {ssl, 1, 0}); // SSL_VERIFY_PEER; original verification code.
        return result;
    } catch (...) {
        std::fputs("native local TLS configuration failed\n", stderr);
        return 0;
    }
}
} // namespace nova::net
