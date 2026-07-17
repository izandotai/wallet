#include <doctest/doctest.h>

#include <string>

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
