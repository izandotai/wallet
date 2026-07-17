#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace izan::crypto {

using Seed = std::array<uint8_t, 64>;

// BIP-39: mnemonic sentence + optional passphrase → 64-byte binary seed.
// The mnemonic is not validated here; run mnemonic_valid first whenever the
// input comes from the user.
Seed mnemonic_to_seed(std::string_view mnemonic, std::string_view passphrase);

// Wordlist membership + checksum.
bool mnemonic_valid(std::string_view mnemonic);

}
