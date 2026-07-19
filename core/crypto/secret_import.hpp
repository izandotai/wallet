#pragma once

#include <string_view>

#include "core/secure/secure_bytes.hpp"

namespace izan::crypto {

// What a pasted wallet secret turned out to be. Content decides: a
// valid BIP-39 sentence, exactly 64 hex digits, a WIF string, a Solana
// keypair — anything else is refused, never guessed at.
enum class SecretKind {
    Unrecognized,
    Mnemonic, // valid BIP-39 sentence; the words stay in the caller's buffer
    RawKey,   // 64 hex digits, optional 0x prefix — secp256k1
    Wif,      // Base58Check, version 0x80 — secp256k1
    SolKey,   // base58 of 64 bytes seed||pubkey, self-verified — ed25519
    // Not secrets at all: watch-only addresses, one kind per family.
    EthAddress,
    BtcAddress,
    SolAddress,
};

struct DetectedSecret {
    SecretKind kind = SecretKind::Unrecognized;
    // RawKey/Wif: the 32-byte secp256k1 scalar; SolKey: the 32-byte
    // ed25519 seed; else empty.
    secure::SecureBytes key;
};

DetectedSecret detect_secret(std::string_view text);

inline bool is_watch_kind(SecretKind kind)
{
    return kind == SecretKind::EthAddress || kind == SecretKind::BtcAddress
        || kind == SecretKind::SolAddress;
}

// The chain-family key a watch address belongs to, in the registry's
// vocabulary; empty for actual secrets.
inline const char* watch_family(SecretKind kind)
{
    switch (kind) {
    case SecretKind::EthAddress:
        return "evm";
    case SecretKind::BtcAddress:
        return "btc";
    case SecretKind::SolAddress:
        return "sol";
    default:
        return "";
    }
}

}
