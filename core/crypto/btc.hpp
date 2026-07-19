#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/secure/secure_bytes.hpp"

namespace izan::crypto {

// Native segwit P2WPKH address (bech32, witness v0) from a compressed
// secp256k1 public key. hrp is "bc" for mainnet, "tb" for testnet.
std::string btc_p2wpkh_address(
    std::span<const uint8_t, 33> pubkey, const char* hrp = "bc");

// Legacy P2PKH address (Base58Check, version 0x00).
std::string btc_p2pkh_address(std::span<const uint8_t, 33> pubkey);

// Nested segwit P2SH-P2WPKH address (BIP-49; Base58Check, version 0x05):
// the P2WPKH witness program wrapped in a script hash, for wallets that
// predate native bech32 support.
std::string btc_p2sh_p2wpkh_address(std::span<const uint8_t, 33> pubkey);

// Taproot P2TR address (BIP-86 key path: the internal key tweaked with
// an empty script tree; bech32m, witness v1).
std::string btc_p2tr_address(
    std::span<const uint8_t, 33> pubkey, const char* hrp = "bc");

// WIF (Base58Check, version 0x80) → the raw 32-byte secp256k1 key in
// guarded memory; the trailing 0x01 compressed marker is accepted and
// dropped — the key is the key. Anything else (bad checksum, wrong
// version, wrong length, zero scalar) is nullopt, never a guess.
// Script-hash addresses: not derived from a key path but from a
// witnessScript (multisig policies, arbitrary spend conditions). The
// script comes from a policy layer above; these only dress its hash.
// P2WSH — witness v0, 32-byte program = sha256(script) → "bc1q…" (62).
std::string btc_p2wsh_address(
    std::span<const uint8_t> witness_script, const char* hrp = "bc");
// P2SH-P2WSH — the same program nested for legacy senders → "3…".
std::string btc_p2sh_p2wsh_address(std::span<const uint8_t> witness_script);

// BIP-340 Schnorr, the taproot signature: 64 bytes over a 32-byte
// message with deterministic-plus-aux nonces. Throws on a zero or
// out-of-range key. Pinned to the BIP's own test vectors.
std::array<uint8_t, 64> bip340_sign(std::span<const uint8_t, 32> seckey,
    std::span<const uint8_t, 32> msg, std::span<const uint8_t, 32> aux);

// The key that actually signs a taproot key-path spend: the private
// key normalized to its even-Y form, tweaked by TapTweak(P.x) — the
// same commitment btc_p2tr_address bakes into the output key.
std::array<uint8_t, 32> bip341_tweak_seckey(std::span<const uint8_t, 32> sk);

std::optional<secure::SecureBytes> wif_to_key(std::string_view wif);

// True iff the text is a Bitcoin mainnet address this wallet can
// reason about: base58check version 0x00 (P2PKH) or 0x05 (P2SH), or
// bech32/bech32m under "bc" (segwit v0/v1). Decoding decides — a
// flipped character fails its checksum, never passes on looks.
bool valid_btc_address(std::string_view text);

}
