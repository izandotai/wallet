#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <string_view>

#include <sodium.h>

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

TEST_CASE("bip39: entropy and mnemonic round-trip both directions")
{
    using izan::secure::SecureBytes;

    // Official vector: 16 zero bytes. (Guarded allocations arrive
    // deliberately filled with junk — zero explicitly.)
    SecureBytes zeros(16);
    sodium_memzero(zeros.data(), zeros.size());
    SecureBytes sentence = izan::crypto::entropy_to_mnemonic(zeros);
    CHECK(std::string_view(reinterpret_cast<const char*>(sentence.data()))
        == "abandon abandon abandon abandon abandon abandon abandon abandon "
           "abandon abandon abandon about");

    SecureBytes back = izan::crypto::mnemonic_to_entropy(
        reinterpret_cast<const char*>(sentence.data()));
    REQUIRE(back.size() == 16);
    CHECK(std::memcmp(back.data(), zeros.data(), 16) == 0);

    // Random 32-byte entropy survives the round trip.
    SecureBytes random32(32);
    randombytes_buf(random32.data(), random32.size());
    SecureBytes words = izan::crypto::entropy_to_mnemonic(random32);
    SecureBytes again = izan::crypto::mnemonic_to_entropy(
        reinterpret_cast<const char*>(words.data()));
    REQUIRE(again.size() == 32);
    CHECK(std::memcmp(again.data(), random32.data(), 32) == 0);

    CHECK_THROWS(izan::crypto::mnemonic_to_entropy("not a mnemonic at all"));
    SecureBytes bad(15);
    CHECK_THROWS(izan::crypto::entropy_to_mnemonic(bad));
}
