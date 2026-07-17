#include "core/crypto/eth.hpp"

#include <array>
#include <cctype>

extern "C" {
#include <sha3.h>
}

namespace izan::crypto {

std::string eth_checksum_address(std::string_view hex)
{
    if (hex.starts_with("0x") || hex.starts_with("0X"))
        hex.remove_prefix(2);
    if (hex.size() != 40)
        return {};
    std::string lower(hex);
    for (char& c : lower) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return {};
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // EIP-55: hash the lowercase ASCII hex; a letter is uppercased when the
    // hash nibble at its position is ≥ 8.
    std::array<uint8_t, 32> digest;
    keccak_256(reinterpret_cast<const uint8_t*>(lower.data()), lower.size(),
        digest.data());
    std::string out = "0x";
    for (size_t i = 0; i < lower.size(); ++i) {
        const uint8_t nibble
            = i % 2 == 0 ? digest[i / 2] >> 4 : digest[i / 2] & 0xf;
        out += lower[i] >= 'a' && nibble >= 8
            ? static_cast<char>(
                  std::toupper(static_cast<unsigned char>(lower[i])))
            : lower[i];
    }
    return out;
}

std::string eth_address(std::span<const uint8_t, 65> pubkey)
{
    std::array<uint8_t, 32> digest;
    keccak_256(pubkey.data() + 1, 64, digest.data());
    std::string hex;
    hex.reserve(40);
    static constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 12; i < 32; ++i) {
        hex += digits[digest[i] >> 4];
        hex += digits[digest[i] & 0xf];
    }
    return eth_checksum_address(hex);
}

}
