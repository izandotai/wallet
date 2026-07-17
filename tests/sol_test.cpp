#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/crypto/sol.hpp"

#include "data/slip10_ed25519_vectors.inc"

namespace {

std::vector<uint8_t> unhex(std::string_view hex)
{
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const auto nibble = [&](char c) -> uint8_t {
            return c <= '9' ? c - '0' : c - 'a' + 10;
        };
        out[i] = nibble(hex[2 * i]) << 4 | nibble(hex[2 * i + 1]);
    }
    return out;
}

template <size_t N>
std::string hex(const std::array<uint8_t, N>& bytes)
{
    std::string out;
    for (uint8_t b : bytes) {
        static constexpr char digits[] = "0123456789abcdef";
        out += digits[b >> 4];
        out += digits[b & 0xf];
    }
    return out;
}

}

TEST_CASE("SLIP-0010 ed25519 official vectors: seed → chain → keys")
{
    for (const auto& v : kSlip10Ed25519Vectors) {
        CAPTURE(v.path);
        const auto key = izan::crypto::sol_derive(unhex(v.seed_hex), v.path);
        REQUIRE(key);
        CHECK(hex(key->private_key) == v.private_hex);
        CHECK("00" + hex(key->public_key) == v.public_hex);
    }
}

TEST_CASE("SLIP-0010 ed25519 rejects non-hardened segments")
{
    const auto seed = unhex("000102030405060708090a0b0c0d0e0f");
    CHECK(!izan::crypto::sol_derive(seed, "m/0"));
    CHECK(izan::crypto::sol_derive(seed, "m/44'/501'/0'/0'"));
}

TEST_CASE("Solana address encoding: the system program is 32 zero bytes")
{
    const std::array<uint8_t, 32> zeros {};
    CHECK(
        izan::crypto::sol_address(zeros) == "11111111111111111111111111111111");
}
