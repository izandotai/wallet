#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace izan::crypto {

// EIP-55 checksummed form of a 40-hex-digit address (with or without 0x,
// any input case). Empty string on malformed input.
std::string eth_checksum_address(std::string_view hex);

// 0x-prefixed EIP-55 address from an uncompressed secp256k1 public key
// (0x04 || X || Y): keccak256 over X||Y, last 20 bytes.
std::string eth_address(std::span<const uint8_t, 65> pubkey);

}
