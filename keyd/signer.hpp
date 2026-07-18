#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "core/crypto/hd.hpp"
#include "core/secure/secure_bytes.hpp"
#include "core/secure/vault.hpp"

namespace izan::keyd {

// A wallet signs as one identity, chosen by its own contents: a seed
// wallet derives its first Ethereum account, a key-only wallet uses
// its imported key directly. More accounts arrive as a payload
// envelope carrying the index, not as a free-form path an attacker
// could point at any branch of the tree.
inline constexpr char kEthAccountPath[] = "m/44'/60'/0'/0/0";

struct SignedDigest {
    crypto::EcdsaSignature sig;
    std::array<uint8_t, 32> digest; // keccak of the payload — the audit anchor
    std::string signer;             // EIP-55 address the signature recovers to
};

// The moment key material meets a transaction: wallet → signing key →
// sign keccak256(payload). Every intermediate (mnemonic, seed, derived
// key) is wiped before this returns; the caller decides what the
// payload means — this function only guarantees the bytes signed are
// exactly the bytes given. Throws on any failure, including a wallet
// with nothing to sign with.
SignedDigest sign_payload(
    const vault::Wallet& wallet, std::span<const uint8_t> payload);

// The wallet's EIP-55 address — the same key selection as
// sign_payload, stopping at the public half.
std::string account_address(const vault::Wallet& wallet);

}
