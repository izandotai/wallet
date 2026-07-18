#pragma once

#include <span>

#include "core/crypto/secret_import.hpp"
#include "keyd/signer.hpp"

namespace izan::ui {

// The preset's display name: a product or standard, never translated.
const char* preset_name(keyd::DerivePreset preset);

// Which presets a recognized secret can wear: a mnemonic derives for
// every family, a secp256k1 key can dress as ETH or any BTC format
// (a WIF arrives in Bitcoin's own clothes, so ETH is not offered),
// and none of the key forms has a Solana self.
std::span<const keyd::DerivePreset> presets_for(crypto::SecretKind kind);

// The preset a freshly recognized secret starts on.
keyd::DerivePreset default_preset(crypto::SecretKind kind);

}
