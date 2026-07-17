#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "core/crypto/bip39.hpp"

#include "data/bip39_vectors.inc"

namespace {

std::string hex(const izan::crypto::Seed& seed)
{
    std::string out;
    out.reserve(seed.size() * 2);
    for (uint8_t b : seed) {
        static constexpr char digits[] = "0123456789abcdef";
        out += digits[b >> 4];
        out += digits[b & 0xf];
    }
    return out;
}

}

TEST_CASE("BIP-39 official vectors: mnemonic → seed, passphrase TREZOR")
{
    for (const auto& v : kBip39Vectors) {
        CAPTURE(v.mnemonic);
        CHECK(izan::crypto::mnemonic_valid(v.mnemonic));
        CHECK(hex(izan::crypto::mnemonic_to_seed(v.mnemonic, "TREZOR"))
            == v.seed);
    }
}

TEST_CASE("BIP-39 rejects a bad checksum")
{
    // 12 × "abandon": right words, wrong checksum (the valid sentence ends
    // in "about").
    CHECK(!izan::crypto::mnemonic_valid(
        "abandon abandon abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon"));
}

TEST_CASE("BIP-39 rejects words outside the list")
{
    CHECK(!izan::crypto::mnemonic_valid(
        "zebra zebra zebra zebra zebra zebra zebra zebra zebra zebra zebra "
        "xyzzy"));
}
