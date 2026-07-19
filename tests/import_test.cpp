// What a pasted wallet secret turns out to be — the import wizard's
// recognition line rides on this classification, so a wrong guess here
// becomes a wallet built around the wrong key.

#include <doctest/doctest.h>

#include <cstring>
#include <span>
#include <string>

#include "core/crypto/secret_import.hpp"
#include "core/crypto/sol.hpp"

using izan::crypto::detect_secret;
using izan::crypto::SecretKind;

namespace {

// The Bitcoin wiki's canonical key, in both of its costumes.
constexpr const char* kKeyHex
    = "0c28fca386c7a227600b2fe50b7cae11ec86d3bf1fbe471be89827e19d72aa1d";
constexpr const char* kKeyWif
    = "5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ";

}

TEST_CASE("a BIP-39 sentence is recognized as a mnemonic")
{
    const auto hit
        = detect_secret("abandon abandon abandon abandon abandon abandon "
                        "abandon abandon abandon abandon abandon about");
    CHECK(hit.kind == SecretKind::Mnemonic);
    CHECK(hit.key.empty());

    // Pasted text arrives with stray whitespace; the secret is the words.
    const auto padded
        = detect_secret("  legal winner thank year wave sausage worth useful "
                        "legal winner thank yellow\n");
    CHECK(padded.kind == SecretKind::Mnemonic);

    // Right words, wrong checksum: not a mnemonic, and since it is
    // neither hex nor WIF, not anything else either.
    const auto bad
        = detect_secret("abandon abandon abandon abandon abandon abandon "
                        "abandon abandon abandon abandon abandon abandon");
    CHECK(bad.kind == SecretKind::Unrecognized);
}

TEST_CASE("64 hex digits are a raw key, with or without 0x")
{
    const auto plain = detect_secret(kKeyHex);
    REQUIRE(plain.kind == SecretKind::RawKey);
    REQUIRE(plain.key.size() == 32);
    CHECK(plain.key.data()[0] == 0x0c);
    CHECK(plain.key.data()[31] == 0x1d);

    const auto prefixed = detect_secret("0x" + std::string(kKeyHex));
    REQUIRE(prefixed.kind == SecretKind::RawKey);
    CHECK(std::memcmp(prefixed.key.data(), plain.key.data(), 32) == 0);

    // 63 digits is a typo, not a shorter key.
    CHECK(detect_secret(std::string(kKeyHex).substr(1)).kind
        == SecretKind::Unrecognized);
    // The zero scalar cannot sign anything.
    CHECK(detect_secret(std::string(64, '0')).kind == SecretKind::Unrecognized);
}

TEST_CASE("a WIF string decodes to the same key its hex form carries")
{
    const auto wif = detect_secret(kKeyWif);
    REQUIRE(wif.kind == SecretKind::Wif);
    REQUIRE(wif.key.size() == 32);

    const auto hex = detect_secret(kKeyHex);
    CHECK(std::memcmp(wif.key.data(), hex.key.data(), 32) == 0);

    // One flipped character breaks the checksum: refused, not guessed.
    std::string tampered(kKeyWif);
    tampered[10] = tampered[10] == 'D' ? 'E' : 'D';
    CHECK(detect_secret(tampered).kind == SecretKind::Unrecognized);
}

TEST_CASE("a Solana keypair decodes to its seed, an address does not")
{
    // base58(seed || pubkey) for the zero-mnemonic Phantom account 0 —
    // the pubkey half must match the seed, which is the integrity
    // check no checksum could give.
    const char* keypair = "27npWoNE4HfmLeQo1TyWcW7NEA28qnsnDK7kcttDQEWr"
                          "CWnro83HMJ97rMmpvYYZRwDAvG4KRuB7hTBacvwD7bgi";
    const auto hit = detect_secret(keypair);
    REQUIRE(hit.kind == SecretKind::SolKey);
    REQUIRE(hit.key.size() == 32);

    // The same 64 bytes with a corrupted digit: pubkey mismatch, refused.
    std::string bad(keypair);
    bad[40] = bad[40] == 'M' ? 'N' : 'M';
    CHECK(detect_secret(bad).kind == SecretKind::Unrecognized);

    // A Solana ADDRESS is 32 bytes of base58 — never a key; since the
    // watch era it is a wallet to observe instead.
    CHECK(detect_secret("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk").kind
        == SecretKind::SolAddress);

    // Backup round-trip: seed → base58(seed||pub) → the original text.
    const izan::secure::SecureBytes enc = izan::crypto::sol_key_to_base58(
        std::span<const uint8_t, 32>(hit.key.data(), 32));
    CHECK(std::string(reinterpret_cast<const char*>(enc.data())) == keypair);
}

TEST_CASE("anything else is refused")
{
    CHECK(detect_secret("").kind == SecretKind::Unrecognized);
    CHECK(detect_secret("   \n\t ").kind == SecretKind::Unrecognized);
    CHECK(detect_secret("hello world").kind == SecretKind::Unrecognized);
    // A P2PKH address is no secret — since the watch era it reads as
    // a Bitcoin wallet to observe.
    CHECK(detect_secret("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2").kind
        == SecretKind::BtcAddress);
}
