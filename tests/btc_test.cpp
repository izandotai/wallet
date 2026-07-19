#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "core/crypto/bip39.hpp"
#include "core/crypto/btc.hpp"
#include "core/crypto/hd.hpp"

#include "data/bip84_vectors.inc"

TEST_CASE("BIP-84 official vectors: mnemonic → P2WPKH addresses")
{
    const auto seed = izan::crypto::mnemonic_to_seed(kBip84Mnemonic, "");
    const auto root = izan::crypto::HdKey::from_seed(seed);
    REQUIRE(root);
    for (const auto& v : kBip84Vectors) {
        CAPTURE(v.path);
        const auto key = root->derive(v.path);
        REQUIRE(key);
        CHECK(izan::crypto::btc_p2wpkh_address(key->public_key_compressed())
            == v.address);
    }
}

TEST_CASE("WIF decodes to its key or to nothing")
{
    // The Bitcoin wiki's canonical example: uncompressed WIF for key
    // 0x0C28FCA3…AA1D.
    const char* wif = "5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ";
    const uint8_t expect[32]
        = { 0x0C, 0x28, 0xFC, 0xA3, 0x86, 0xC7, 0xA2, 0x27, 0x60, 0x0B, 0x2F,
              0xE5, 0x0B, 0x7C, 0xAE, 0x11, 0xEC, 0x86, 0xD3, 0xBF, 0x1F, 0xBE,
              0x47, 0x1B, 0xE8, 0x98, 0x27, 0xE1, 0x9D, 0x72, 0xAA, 0x1D };
    auto key = izan::crypto::wif_to_key(wif);
    REQUIRE(key);
    REQUIRE(key->size() == 32);
    CHECK(std::memcmp(key->data(), expect, 32) == 0);

    // One flipped character breaks the checksum: refused, not guessed.
    std::string tampered(wif);
    tampered[10] = tampered[10] == 'D' ? 'E' : 'D';
    CHECK(!izan::crypto::wif_to_key(tampered));

    CHECK(!izan::crypto::wif_to_key(""));
    CHECK(!izan::crypto::wif_to_key("not-a-wif"));
    // A valid Base58Check string of the wrong shape (a P2PKH address)
    // is not a key either.
    CHECK(!izan::crypto::wif_to_key("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2"));
}

TEST_CASE("script-hash addresses dress a witnessScript, both costumes")
{
    // BIP-173's own P2WSH example: push33(generator pubkey) CHECKSIG.
    const uint8_t script[] = { 0x21, 0x02, 0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc,
        0xbb, 0xac, 0x55, 0xa0, 0x62, 0x95, 0xce, 0x87, 0x0b, 0x07, 0x02, 0x9b,
        0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9, 0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8,
        0x17, 0x98, 0xac };
    CHECK(izan::crypto::btc_p2wsh_address(script)
        == "bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3");
    // The nested form, anchored by the independent pure-python
    // implementation in tests/data/derive_vectors.py.
    CHECK(izan::crypto::btc_p2sh_p2wsh_address(script)
        == "3NVZWnhKt53ukKw4Qm217Zk57FE8VnKjH2");
    // Both read as valid recipients — the watch/send gate speaks them.
    CHECK(izan::crypto::valid_btc_address(
        "bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3"));
    CHECK(
        izan::crypto::valid_btc_address("3NVZWnhKt53ukKw4Qm217Zk57FE8VnKjH2"));
}
