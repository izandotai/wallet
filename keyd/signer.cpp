#include "keyd/signer.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#include <sodium.h>

extern "C" {
// Upstream header trips C++20's volatile-parameter deprecation; not ours
// to fix, not ours to drown in either.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include <ecdsa.h>
#include <memzero.h>
#include <secp256k1.h>
#include <sha3.h>
#pragma GCC diagnostic pop
}

#include "core/crypto/bip39.hpp"
#include "core/crypto/btc.hpp"
#include "core/crypto/eth.hpp"
#include "core/crypto/sol.hpp"

namespace izan::keyd {

namespace {

    crypto::Seed entropy_seed(const secure::SecureBytes& entropy)
    {
        const secure::SecureBytes mnemonic
            = crypto::entropy_to_mnemonic(entropy);
        return crypto::mnemonic_to_seed(
            reinterpret_cast<const char*>(mnemonic.data()), "");
    }

    crypto::HdKey account_key(const secure::SecureBytes& entropy,
        uint32_t account, DerivePreset preset)
    {
        crypto::Seed seed = entropy_seed(entropy);
        std::optional<crypto::HdKey> root = crypto::HdKey::from_seed(seed);
        sodium_memzero(seed.data(), seed.size());
        if (!root)
            throw std::runtime_error("signer: seed rejected");
        std::optional<crypto::HdKey> key
            = root->derive(derive_path(preset, account));
        if (!key)
            throw std::runtime_error("signer: account underivable");
        return *key;
    }

    std::string sol_account_address(
        const secure::SecureBytes& entropy, uint32_t account)
    {
        crypto::Seed seed = entropy_seed(entropy);
        const std::optional<crypto::SolKey> key = crypto::sol_derive(
            seed, derive_path(DerivePreset::SolPhantom, account));
        sodium_memzero(seed.data(), seed.size());
        if (!key)
            throw std::runtime_error("signer: account underivable");
        return crypto::sol_address(key->public_key);
    }

    // The address a secp256k1 public key wears under a BTC preset —
    // for these, the preset byte doubles as the address format.
    std::string btc_preset_address(
        DerivePreset preset, std::span<const uint8_t, 33> pub)
    {
        switch (preset) {
        case DerivePreset::BtcLegacy:
            return crypto::btc_p2pkh_address(pub);
        case DerivePreset::BtcNestedSegwit:
            return crypto::btc_p2sh_p2wpkh_address(pub);
        case DerivePreset::BtcSegwit:
            return crypto::btc_p2wpkh_address(pub);
        case DerivePreset::BtcTaproot:
            return crypto::btc_p2tr_address(pub);
        default:
            throw std::invalid_argument("signer: not a BTC preset");
        }
    }

    // The imported-key path: no derivation, the key IS the identity.
    void sign_with_raw(const secure::SecureBytes& key,
        std::span<const uint8_t, 32> digest, SignedDigest& out)
    {
        if (key.size() != 32)
            throw std::runtime_error("signer: malformed imported key");
        uint8_t sig[64];
        uint8_t parity = 0;
        if (ecdsa_sign_digest(
                &secp256k1, key.data(), digest.data(), sig, &parity, nullptr)
            != 0)
            throw std::runtime_error("signer: signing failed");
        std::memcpy(out.sig.r.data(), sig, 32);
        std::memcpy(out.sig.s.data(), sig + 32, 32);
        out.sig.y_parity = parity;
        memzero(sig, sizeof sig);

        uint8_t pub[65];
        ecdsa_get_public_key65(&secp256k1, key.data(), pub);
        out.signer = crypto::eth_address(std::span<const uint8_t, 65>(pub, 65));
    }

}

ChainFamily preset_family(DerivePreset preset)
{
    switch (preset) {
    case DerivePreset::MetaMask:
    case DerivePreset::LedgerLive:
    case DerivePreset::LegacyMew:
        return ChainFamily::Eth;
    case DerivePreset::BtcLegacy:
    case DerivePreset::BtcNestedSegwit:
    case DerivePreset::BtcSegwit:
    case DerivePreset::BtcTaproot:
        return ChainFamily::Btc;
    case DerivePreset::SolPhantom:
        return ChainFamily::Sol;
    }
    throw std::invalid_argument("signer: unknown derive preset");
}

std::string derive_path(DerivePreset preset, uint32_t account)
{
    const std::string i = std::to_string(account);
    switch (preset) {
    case DerivePreset::MetaMask:
        return "m/44'/60'/0'/0/" + i;
    case DerivePreset::LedgerLive:
        return "m/44'/60'/" + i + "'/0/0";
    case DerivePreset::LegacyMew:
        return "m/44'/60'/0'/" + i;
    case DerivePreset::BtcLegacy:
        return "m/44'/0'/0'/0/" + i;
    case DerivePreset::BtcNestedSegwit:
        return "m/49'/0'/0'/0/" + i;
    case DerivePreset::BtcSegwit:
        return "m/84'/0'/0'/0/" + i;
    case DerivePreset::BtcTaproot:
        return "m/86'/0'/0'/0/" + i;
    case DerivePreset::SolPhantom:
        return "m/44'/501'/" + i + "'/0'";
    }
    throw std::invalid_argument("signer: unknown derive preset");
}

ProposalBody parse_proposal(std::span<const uint8_t> payload)
{
    const auto u32le = [](std::span<const uint8_t> p) {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
            | uint32_t(p[3]) << 24;
    };
    ProposalBody body;
    if (!payload.empty() && payload.front() == kEnvelopeV1) {
        if (payload.size() < 5)
            throw std::invalid_argument("signer: truncated envelope");
        body.account = u32le(payload.subspan(1));
        body.tx = payload.subspan(5);
    } else if (!payload.empty() && payload.front() == kEnvelopeV2) {
        if (payload.size() < 6)
            throw std::invalid_argument("signer: truncated envelope");
        if (payload[1] >= kDerivePresetCount)
            throw std::invalid_argument("signer: unknown derive preset");
        body.preset = DerivePreset(payload[1]);
        body.account = u32le(payload.subspan(2));
        body.tx = payload.subspan(6);
    } else {
        body.tx = payload;
    }
    if (body.tx.empty())
        throw std::invalid_argument("signer: empty payload");
    return body;
}

SignedDigest sign_payload(const vault::Wallet& wallet,
    std::span<const uint8_t> tx, uint32_t account, DerivePreset preset)
{
    if (tx.empty())
        throw std::invalid_argument("signer: empty payload");
    // Only EVM transactions exist to sign so far. BTC and SOL presets
    // are receive-side identities; their spend paths arrive with their
    // own transaction engines, not by signing EVM bytes under a
    // borrowed path.
    if (preset_family(preset) != ChainFamily::Eth)
        throw std::invalid_argument(
            "signer: this chain family cannot sign transactions yet");

    SignedDigest out;
    keccak_256(tx.data(), tx.size(), out.digest.data());

    if (!wallet.entropy.empty()) {
        const crypto::HdKey key = account_key(wallet.entropy, account, preset);
        const std::optional<crypto::EcdsaSignature> sig
            = key.sign_digest(out.digest);
        if (!sig)
            throw std::runtime_error("signer: signing failed");
        out.sig = *sig;
        out.signer = crypto::eth_address(key.public_key_uncompressed());
    } else if (!wallet.imported.empty()) {
        if (account != 0)
            throw std::invalid_argument(
                "signer: a key wallet has a single address");
        sign_with_raw(wallet.imported.front().key, out.digest, out);
    } else {
        throw std::invalid_argument("signer: wallet holds no signing key");
    }
    return out;
}

std::string account_address(
    const vault::Wallet& wallet, uint32_t account, DerivePreset preset)
{
    const ChainFamily family = preset_family(preset);
    if (!wallet.entropy.empty()) {
        if (family == ChainFamily::Sol)
            return sol_account_address(wallet.entropy, account);
        const crypto::HdKey key = account_key(wallet.entropy, account, preset);
        if (family == ChainFamily::Eth)
            return crypto::eth_address(key.public_key_uncompressed());
        return btc_preset_address(preset, key.public_key_compressed());
    }
    if (!wallet.imported.empty()) {
        if (account != 0)
            throw std::invalid_argument(
                "signer: a key wallet has a single address");
        if (family == ChainFamily::Sol)
            throw std::invalid_argument(
                "signer: a secp256k1 key has no Solana identity");
        const secure::SecureBytes& key = wallet.imported.front().key;
        if (key.size() != 32)
            throw std::runtime_error("signer: malformed imported key");
        if (family == ChainFamily::Eth) {
            uint8_t pub[65];
            ecdsa_get_public_key65(&secp256k1, key.data(), pub);
            return crypto::eth_address(std::span<const uint8_t, 65>(pub, 65));
        }
        uint8_t pub[33];
        ecdsa_get_public_key33(&secp256k1, key.data(), pub);
        return btc_preset_address(
            preset, std::span<const uint8_t, 33>(pub, 33));
    }
    throw std::invalid_argument("signer: wallet holds no signing key");
}

}
