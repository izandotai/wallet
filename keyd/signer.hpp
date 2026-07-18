#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "core/crypto/hd.hpp"
#include "core/secure/secure_bytes.hpp"
#include "core/secure/vault.hpp"

namespace izan::keyd {

// A wallet's identities, chosen by its own contents: a seed wallet
// derives an account line (one address per index — HD means endless
// addresses), a key-only wallet IS its single key. Index and preset
// arrive through the proposal envelope below, never as a free-form
// path an attacker could point at any branch of the tree.

// The fixed menu of derivation schemes the industry actually uses;
// importing another vendor's mnemonic means matching its paths. A
// registry, not an input field — paths are security surface. A preset
// names a chain family, a path template and an address format in one
// byte; for a key-only wallet the path is moot and the preset is just
// the address format its single key wears.
enum class DerivePreset : uint8_t {
    MetaMask = 0,        // m/44'/60'/0'/0/{i} — MetaMask, Trezor, most
    LedgerLive = 1,      // m/44'/60'/{i}'/0/0
    LegacyMew = 2,       // m/44'/60'/0'/{i}
    BtcLegacy = 3,       // m/44'/0'/0'/0/{i} — P2PKH (1…)
    BtcNestedSegwit = 4, // m/49'/0'/0'/0/{i} — P2SH-P2WPKH (3…)
    BtcSegwit = 5,       // m/84'/0'/0'/0/{i} — P2WPKH (bc1q…)
    BtcTaproot = 6,      // m/86'/0'/0'/0/{i} — P2TR (bc1p…)
    SolPhantom = 7,      // m/44'/501'/{i}'/0' — SLIP-0010, Phantom/Sollet
};
inline constexpr uint8_t kDerivePresetCount = 8;

enum class ChainFamily : uint8_t { Eth, Btc, Sol };

// Which chain a preset speaks for. Total over the registry — an
// unknown preset never reaches this (parse and UI both bound-check).
ChainFamily preset_family(DerivePreset preset);

// The derivation path a preset assigns to an account index (BIP-32 for
// the secp256k1 families, SLIP-0010 hardened-only for Solana). Throws
// on an unknown preset — an unrecognized byte must not silently derive.
std::string derive_path(DerivePreset preset, uint32_t account);

struct SignedDigest {
    crypto::EcdsaSignature sig;
    std::array<uint8_t, 32> digest; // keccak of the tx bytes — audit anchor
    std::string signer;             // EIP-55 address the signature recovers to
};

// Proposal payload envelopes. Bare payloads (typed transactions start
// 0x02) are account #0 under the MetaMask preset, so envelope-less
// submitters keep working. The envelope is part of what sits in the
// queue: the identity the human approves is the identity that signs.
//   v1: 0x01 | account(u32 LE) | tx
//   v2: 0x03 | preset(1) | account(u32 LE) | tx
inline constexpr uint8_t kEnvelopeV1 = 0x01;
inline constexpr uint8_t kEnvelopeV2 = 0x03;

struct ProposalBody {
    DerivePreset preset = DerivePreset::MetaMask;
    uint32_t account = 0;
    std::span<const uint8_t> tx; // the bytes whose keccak gets signed
};

// Splits an envelope off a queued payload. Throws on a truncated
// envelope or an empty tx body.
ProposalBody parse_proposal(std::span<const uint8_t> payload);

// The moment key material meets a transaction: wallet + account index
// → signing key → sign keccak256(tx). Every intermediate (mnemonic,
// seed, derived key) is wiped before this returns; the caller decides
// what the bytes mean — this function only guarantees the bytes signed
// are exactly the bytes given. Throws on any failure: a wallet with
// nothing to sign with, or an index a key-only wallet cannot have.
SignedDigest sign_payload(const vault::Wallet& wallet,
    std::span<const uint8_t> tx, uint32_t account = 0,
    DerivePreset preset = DerivePreset::MetaMask);

// The account's EIP-55 address — the same key selection as
// sign_payload, stopping at the public half.
std::string account_address(const vault::Wallet& wallet, uint32_t account = 0,
    DerivePreset preset = DerivePreset::MetaMask);

}
