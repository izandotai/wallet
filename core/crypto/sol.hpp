#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/secure/secure_bytes.hpp"

namespace izan::crypto {

// SLIP-0010 ed25519 key. Wipes itself on destruction.
struct SolKey {
    std::array<uint8_t, 32> private_key {};
    std::array<uint8_t, 32> public_key {};

    ~SolKey();
    SolKey() = default;
    SolKey(const SolKey&) = default;
    SolKey& operator=(const SolKey&) = default;
};

// SLIP-0010 ed25519 derivation. The scheme only defines hardened children;
// a path with a non-hardened segment is rejected.
std::optional<SolKey> sol_derive(
    std::span<const uint8_t> seed, std::string_view path);

// Solana address = base58 of the ed25519 public key.
std::string sol_address(std::span<const uint8_t, 32> pubkey);

// The address a bare ed25519 seed answers to.
std::string sol_key_address(std::span<const uint8_t, 32> seed);

// The 64-byte Solana keypair encoding — plain base58 of seed || pubkey,
// what solana-keygen and Phantom export. Decodes to the 32-byte seed in
// guarded memory after verifying the embedded pubkey really belongs to
// the seed; a mismatch or any other shape is nullopt, never a guess.
// (A bare 44-char base58 string is an ADDRESS and is deliberately not
// accepted as a key.)
std::optional<secure::SecureBytes> sol_key_from_base58(std::string_view text);

// The inverse, for backup display: NUL-terminated base58(seed||pubkey)
// in guarded memory.
secure::SecureBytes sol_key_to_base58(std::span<const uint8_t, 32> seed);

// RFC 8032 ed25519 signature over the WHOLE message with the 32-byte
// seed — Solana signs raw transaction messages, never a pre-hash.
std::array<uint8_t, 64> sol_sign(
    std::span<const uint8_t, 32> seed, std::span<const uint8_t> message);

// True iff the text decodes as base58 to exactly 32 bytes — a Solana
// account address. Pure; no network.
bool valid_sol_address(std::string_view text);

}
