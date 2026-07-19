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

TEST_CASE("BIP-340 signs the spec's own vectors")
{
    auto unhex32 = [](const char* h) {
        std::array<uint8_t, 32> out {};
        for (int i = 0; i < 32; ++i) {
            auto nib = [](char c) {
                return uint8_t(c <= '9' ? c - '0' : (c & 0x5f) - 'A' + 10);
            };
            out[std::size_t(i)]
                = uint8_t(nib(h[2 * i]) << 4 | nib(h[2 * i + 1]));
        }
        return out;
    };

    struct V {
        const char* sk;
        const char* aux;
        const char* msg;
        const char* sig;
    };

    // bitcoin/bips bip-0340/test-vectors.csv, rows 0-3, fetched from
    // the source on 2026-07-19.
    const V vs[] = {
        { "0000000000000000000000000000000000000000000000000000000000000003",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "E907831F80848D1069A5371B402410364BDF1C5F8307B0084C55F1CE2DCA8215"
            "25F66A4A85EA8B71E482A74F382D2CE5EBEEE8FDB2172F477DF4900D310536C"
            "0" },
        { "B7E151628AED2A6ABF7158809CF4F3C762E7160F38B4DA56A784D9045190CFEF",
            "0000000000000000000000000000000000000000000000000000000000000001",
            "243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89",
            "6896BD60EEAE296DB48A229FF71DFE071BDE413E6D43F917DC8DCF8C78DE3341"
            "8906D11AC976ABCCB20B091292BFF4EA897EFCB639EA871CFA95F6DE339E4B0"
            "A" },
        { "C90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B14E5C9",
            "C87AA53824B4D7AE2EB035A2B5BBBCCC080E76CDC6D1692C4B0B62D798E6D906",
            "7E2D58D8B3BCDF1ABADEC7829054F90DDA9805AAB56C77333024B9D0A508B75C",
            "5831AAEED7B44BB74E5EAB94BA9D4294C49BCF2A60728D8B4C200F50DD313C1B"
            "AB745879A5AD954A72C45A91C3A51D3C7ADEA98D82F8481E0E1E03674A6F3FB"
            "7" },
        { "0B432B2677937381AEF05BB02A66ECD012773062CF3FA2549E44F58ED2401710",
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
            "7EB0509757E246F19449885651611CB965ECC1A187DD51B64FDA1EDC9637D5EC"
            "97582B9CB13DB3933705B32BA982AF5AF25FD78881EBB32771FC5922EFC66EA"
            "3" },
    };
    for (const V& v : vs) {
        const auto sig = izan::crypto::bip340_sign(
            unhex32(v.sk), unhex32(v.msg), unhex32(v.aux));
        std::array<uint8_t, 64> want {};
        for (int i = 0; i < 64; ++i) {
            auto nib = [](char c) {
                return uint8_t(c <= '9' ? c - '0' : (c & 0x5f) - 'A' + 10);
            };
            want[std::size_t(i)]
                = uint8_t(nib(v.sig[2 * i]) << 4 | nib(v.sig[2 * i + 1]));
        }
        CHECK(sig == want);
    }
    // Degenerate keys are refused, not signed with.
    std::array<uint8_t, 32> zero {};
    CHECK_THROWS(izan::crypto::bip340_sign(zero, zero, zero));
}
