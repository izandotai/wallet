#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "core/crypto/hd.hpp"
#include "core/secure/secure_bytes.hpp"

namespace izan::keyd {

// v1 signs as one identity: the wallet's first Ethereum account. More
// accounts arrive as a payload envelope carrying the index, not as a
// free-form path an attacker could point at any branch of the tree.
inline constexpr char kEthAccountPath[] = "m/44'/60'/0'/0/0";

struct SignedDigest {
    crypto::EcdsaSignature sig;
    std::array<uint8_t, 32> digest; // keccak of the payload — the audit anchor
    std::string signer;             // EIP-55 address the signature recovers to
};

// The moment key material meets a transaction: vault entropy → seed →
// account key → sign keccak256(payload). Every intermediate (mnemonic,
// seed, derived key) is wiped before this returns; the caller decides
// what the payload means — this function only guarantees the bytes
// signed are exactly the bytes given. Throws on any failure.
SignedDigest sign_payload(
    const secure::SecureBytes& entropy, std::span<const uint8_t> payload);

// The account's EIP-55 address — the same derivation as sign_payload,
// stopping at the public half. Throws on an empty or rejected seed.
std::string account_address(const secure::SecureBytes& entropy);

}
