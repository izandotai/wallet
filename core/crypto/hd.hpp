#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

extern "C" {
#include <bip32.h>
}

namespace izan::crypto {

// BIP-32 hierarchical deterministic key on secp256k1. Holds private key
// material; every instance wipes itself on destruction.
class HdKey {
public:
    static std::optional<HdKey> from_seed(std::span<const uint8_t> seed);

    // "m/44'/60'/0'/0/0" (absolute) or "0'/1" (relative). Hardened markers:
    // apostrophe or h/H. Returns nullopt on a malformed path or when a child
    // is underivable.
    std::optional<HdKey> derive(std::string_view path) const;

    // BIP-32 serialization, mainnet versions (xprv/xpub). The xprv string
    // contains the private key — treat it like one.
    std::string xprv() const;
    std::string xpub() const;

    // Uncompressed secp256k1 public key, 0x04 || X || Y.
    std::array<uint8_t, 65> public_key_uncompressed() const;

    ~HdKey();
    HdKey(const HdKey&) = default;
    HdKey& operator=(const HdKey&) = default;
    HdKey(HdKey&&) = default;
    HdKey& operator=(HdKey&&) = default;

private:
    HdKey() = default;

    HDNode node_ {};
    uint32_t parent_fingerprint_ = 0;
};

}
