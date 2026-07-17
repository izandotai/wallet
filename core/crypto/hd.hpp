#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

extern "C" {
// Upstream header trips C++20's volatile-parameter deprecation; not ours to
// fix, not ours to drown in either.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include <bip32.h>
#pragma GCC diagnostic pop
}

namespace izan::crypto {

// Recoverable secp256k1 signature over a 32-byte digest: deterministic
// nonce (RFC 6979), canonical low s. y_parity selects which of the two
// candidate public keys recovery yields — exactly the (v, r, s) an
// EIP-1559 transaction carries.
struct EcdsaSignature {
    std::array<uint8_t, 32> r {};
    std::array<uint8_t, 32> s {};
    uint8_t y_parity = 0;
};

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

    // Compressed secp256k1 public key, 33 bytes.
    std::array<uint8_t, 33> public_key_compressed() const;

    // Signs a 32-byte digest with this key. The private key never
    // leaves the class; this is the only way key material meets a
    // hash. nullopt only on an upstream signing failure.
    std::optional<EcdsaSignature> sign_digest(
        std::span<const uint8_t, 32> digest) const;

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
