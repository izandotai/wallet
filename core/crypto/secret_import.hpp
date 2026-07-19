#pragma once

#include <string_view>

#include "core/secure/secure_bytes.hpp"

namespace izan::crypto {

// What a pasted wallet secret turned out to be. Content decides: a
// valid BIP-39 sentence, exactly 64 hex digits, a WIF string, a Solana
// keypair — anything else is refused, never guessed at.
enum class SecretKind {
    Unrecognized,
    Mnemonic,   // valid BIP-39 sentence; the words stay in the caller's buffer
    RawKey,     // 64 hex digits, optional 0x prefix — secp256k1
    Wif,        // Base58Check, version 0x80 — secp256k1
    SolKey,     // base58 of 64 bytes seed||pubkey, self-verified — ed25519
    EthAddress, // not a secret at all: a watch-only EVM address
};

struct DetectedSecret {
    SecretKind kind = SecretKind::Unrecognized;
    // RawKey/Wif: the 32-byte secp256k1 scalar; SolKey: the 32-byte
    // ed25519 seed; else empty.
    secure::SecureBytes key;
};

DetectedSecret detect_secret(std::string_view text);

}
