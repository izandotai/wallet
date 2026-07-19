#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/crypto/secret_import.hpp"
#include "keyd/signer.hpp"
#include "ui/wallet/store.hpp"

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

// The i18n key for a sidecar kind badge (store.hpp kKind*); empty
// string for a legacy sidecar that never recorded one.
const char* kind_badge_key(std::string_view kind);

// The chain registry's word for a keyd family.
const char* family_key(keyd::ChainFamily family);

// All-chain wallets: the chain families a wallet answers for. An HD
// seed carries every family's identity — no choosing at creation; a
// key wallet stays on the single family its stored preset names; a
// watch wallet is its address's family.
std::vector<std::string> wallet_families(const AccountsMeta& meta);

// The derivation preset a wallet wears for a family: its birth preset
// where the family matches (an imported LedgerLive line stays
// LedgerLive), the family's industry default elsewhere — MetaMask,
// native segwit, Phantom.
keyd::DerivePreset family_preset(
    const AccountsMeta& meta, std::string_view family);

}
