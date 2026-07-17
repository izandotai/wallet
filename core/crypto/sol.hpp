#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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

}
