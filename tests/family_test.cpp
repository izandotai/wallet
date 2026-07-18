// Chain-family presets: a preset byte names a chain, a path and an
// address format, and the wallet answers with the right address on
// every one of them. Vectors: BIP-84/86 from their specs; BIP-44/49
// mainnet and Solana cross-checked against an independent
// implementation (tests/data/derive_vectors.py, self-validated
// against the spec vectors before use).

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include <sodium.h>

#include "core/secure/vault.hpp"
#include "keyd/signer.hpp"

using namespace izan;
using keyd::ChainFamily;
using keyd::DerivePreset;

namespace {

// Zero entropy is the "abandon … about" mnemonic every derivation
// spec anchors its examples to.
vault::Wallet zero_seed_wallet()
{
    vault::Wallet w;
    w.entropy = secure::SecureBytes(16);
    sodium_memzero(w.entropy.data(), w.entropy.size());
    return w;
}

// The Bitcoin wiki's canonical example key.
vault::Wallet wiki_key_wallet()
{
    static constexpr uint8_t kKey[32]
        = { 0x0C, 0x28, 0xFC, 0xA3, 0x86, 0xC7, 0xA2, 0x27, 0x60, 0x0B, 0x2F,
              0xE5, 0x0B, 0x7C, 0xAE, 0x11, 0xEC, 0x86, 0xD3, 0xBF, 0x1F, 0xBE,
              0x47, 0x1B, 0xE8, 0x98, 0x27, 0xE1, 0x9D, 0x72, 0xAA, 0x1D };
    vault::Wallet w;
    vault::Imported imp;
    imp.label = "imported";
    imp.key = secure::SecureBytes(32);
    std::memcpy(imp.key.data(), kKey, 32);
    w.imported.push_back(std::move(imp));
    return w;
}

}

TEST_CASE("every preset knows its family and its path")
{
    CHECK(keyd::preset_family(DerivePreset::MetaMask) == ChainFamily::Eth);
    CHECK(keyd::preset_family(DerivePreset::LegacyMew) == ChainFamily::Eth);
    CHECK(keyd::preset_family(DerivePreset::BtcLegacy) == ChainFamily::Btc);
    CHECK(keyd::preset_family(DerivePreset::BtcTaproot) == ChainFamily::Btc);
    CHECK(keyd::preset_family(DerivePreset::SolPhantom) == ChainFamily::Sol);

    CHECK(keyd::derive_path(DerivePreset::BtcLegacy, 7) == "m/44'/0'/0'/0/7");
    CHECK(keyd::derive_path(DerivePreset::BtcNestedSegwit, 0)
        == "m/49'/0'/0'/0/0");
    CHECK(keyd::derive_path(DerivePreset::BtcSegwit, 2) == "m/84'/0'/0'/0/2");
    CHECK(keyd::derive_path(DerivePreset::BtcTaproot, 0) == "m/86'/0'/0'/0/0");
    // Solana is SLIP-0010: hardened all the way down, account in the
    // third slot — the Phantom/Sollet convention.
    CHECK(keyd::derive_path(DerivePreset::SolPhantom, 0) == "m/44'/501'/0'/0'");
    CHECK(keyd::derive_path(DerivePreset::SolPhantom, 3) == "m/44'/501'/3'/0'");
}

TEST_CASE("a seed wallet answers every chain family's address")
{
    const vault::Wallet w = zero_seed_wallet();

    CHECK(keyd::account_address(w, 0, DerivePreset::BtcLegacy)
        == "1LqBGSKuX5yYUonjxT5qGfpUsXKYYWeabA");
    CHECK(keyd::account_address(w, 1, DerivePreset::BtcLegacy)
        == "1Ak8PffB2meyfYnbXZR9EGfLfFZVpzJvQP");

    CHECK(keyd::account_address(w, 0, DerivePreset::BtcNestedSegwit)
        == "37VucYSaXLCAsxYyAPfbSi9eh4iEcbShgf");
    CHECK(keyd::account_address(w, 1, DerivePreset::BtcNestedSegwit)
        == "3LtMnn87fqUeHBUG414p9CWwnoV6E2pNKS");

    // BIP-84's own spec vector.
    CHECK(keyd::account_address(w, 0, DerivePreset::BtcSegwit)
        == "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu");

    // BIP-86's own spec vectors.
    CHECK(keyd::account_address(w, 0, DerivePreset::BtcTaproot)
        == "bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr");
    CHECK(keyd::account_address(w, 1, DerivePreset::BtcTaproot)
        == "bc1p4qhjn9zdvkux4e44uhx8tc55attvtyu358kutcqkudyccelu0was9fqzwh");

    CHECK(keyd::account_address(w, 0, DerivePreset::SolPhantom)
        == "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk");
    CHECK(keyd::account_address(w, 1, DerivePreset::SolPhantom)
        == "Hh8QwFUA6MtVu1qAoq12ucvFHNwCcVTV7hpWjeY1Hztb");
}

TEST_CASE("a key wallet wears any BTC format but has no Solana self")
{
    const vault::Wallet w = wiki_key_wallet();

    CHECK(keyd::account_address(w, 0, DerivePreset::BtcLegacy)
        == "1LoVGDgRs9hTfTNJNuXKSpywcbdvwRXpmK");
    CHECK(keyd::account_address(w, 0, DerivePreset::BtcNestedSegwit)
        == "3D9iyFHi1Zs9KoyynUfrL82rGhJfYTfSG4");
    CHECK(keyd::account_address(w, 0, DerivePreset::BtcSegwit)
        == "bc1qmy63mjadtw8nhzl69ukdepwzsyvv4yex5qlmkd");
    CHECK(keyd::account_address(w, 0, DerivePreset::BtcTaproot)
        == "bc1pdj78vjhv4wukfzfu3qyvwclcewrsyfq8fyx2gvs8s850smsgw0yq8ykfa8");

    // An ed25519 identity cannot be conjured from a secp256k1 key.
    CHECK_THROWS(keyd::account_address(w, 0, DerivePreset::SolPhantom));
    // And a key wallet still has exactly one address.
    CHECK_THROWS(keyd::account_address(w, 1, DerivePreset::BtcSegwit));
}

TEST_CASE("only the EVM family signs transactions for now")
{
    const vault::Wallet w = zero_seed_wallet();
    const std::vector<uint8_t> tx { 0x02, 0x01 };

    CHECK_THROWS(keyd::sign_payload(w, tx, 0, DerivePreset::BtcSegwit));
    CHECK_THROWS(keyd::sign_payload(w, tx, 0, DerivePreset::SolPhantom));
    // The EVM presets still sign.
    CHECK(!keyd::sign_payload(w, tx, 0, DerivePreset::MetaMask).signer.empty());
}

TEST_CASE("the envelope accepts the whole registry and nothing more")
{
    // v2: 0x03 | preset | account(u32 LE) | tx
    std::vector<uint8_t> env { 0x03, 7, 0, 0, 0, 0, 0xAA };
    const keyd::ProposalBody body = keyd::parse_proposal(env);
    CHECK(body.preset == DerivePreset::SolPhantom);
    CHECK(body.account == 0);

    env[1] = keyd::kDerivePresetCount;
    CHECK_THROWS(keyd::parse_proposal(env));
}
