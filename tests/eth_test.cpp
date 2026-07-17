#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <string>

extern "C" {
#include <ecdsa.h>
#include <secp256k1.h>
}

#include "core/crypto/bip39.hpp"
#include "core/crypto/eth.hpp"
#include "core/crypto/hd.hpp"

#include "data/eip55_vectors.inc"

TEST_CASE("EIP-55 spec addresses are checksum fixed points")
{
    for (const char* addr : kEip55Vectors) {
        CAPTURE(addr);
        std::string lower(addr);
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        CHECK(izan::crypto::eth_checksum_address(lower) == addr);
    }
}

TEST_CASE("EIP-55 rejects malformed input")
{
    CHECK(izan::crypto::eth_checksum_address("0x1234").empty());
    CHECK(izan::crypto::eth_checksum_address(
        "zz08400098527886E0F7030069857D2E4169EE7X")
            .empty());
}

TEST_CASE("mnemonic → m/44'/60'/0'/0/0 → well-known address")
{
    // Hardhat/Anvil default account #0 — the most widely cross-checked
    // mnemonic→address pair in the EVM ecosystem.
    const auto seed = izan::crypto::mnemonic_to_seed(
        "test test test test test test test test test test test junk", "");
    const auto root = izan::crypto::HdKey::from_seed(seed);
    REQUIRE(root);
    const auto key = root->derive("m/44'/60'/0'/0/0");
    REQUIRE(key);
    CHECK(izan::crypto::eth_address(key->public_key_uncompressed())
        == "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266");
}

TEST_CASE("sign_digest recovers to the signing key and keeps s low")
{
    const auto seed = izan::crypto::mnemonic_to_seed(
        "test test test test test test test test test test test junk", "");
    const auto root = izan::crypto::HdKey::from_seed(seed);
    REQUIRE(root);
    const auto key = root->derive("m/44'/60'/0'/0/0");
    REQUIRE(key);

    std::array<uint8_t, 32> digest {};
    digest.fill(0x5a);
    const auto sig = key->sign_digest(digest);
    REQUIRE(sig);

    // Ethereum consensus rejects high-s signatures outright (EIP-2):
    // s must stay below half the curve order, so its top byte is small.
    CHECK(sig->s[0] < 0x80);
    CHECK(sig->y_parity <= 1);

    uint8_t rs[64];
    std::memcpy(rs, sig->r.data(), 32);
    std::memcpy(rs + 32, sig->s.data(), 32);
    uint8_t pub[65];
    REQUIRE(ecdsa_recover_pub_from_sig(
                &secp256k1, pub, rs, digest.data(), sig->y_parity)
        == 0);
    CHECK(izan::crypto::eth_address(std::span<const uint8_t, 65>(pub, 65))
        == "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266");
}
