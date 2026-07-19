#include "ui/wallet/presets.hpp"

#include "ui/wallet/store.hpp"

namespace izan::ui {

const char* kind_badge_key(std::string_view kind)
{
    if (kind == kKindHd)
        return "vault.kind.hd";
    if (kind == kKindSecp)
        return "vault.kind.key";
    if (kind == kKindEd25519)
        return "vault.kind.ed25519";
    if (kind == kKindWatch)
        return "vault.kind.watch";
    return "";
}

const char* preset_name(keyd::DerivePreset preset)
{
    switch (preset) {
    case keyd::DerivePreset::MetaMask:
        return "MetaMask";
    case keyd::DerivePreset::LedgerLive:
        return "Ledger Live";
    case keyd::DerivePreset::LegacyMew:
        return "Legacy MEW";
    case keyd::DerivePreset::BtcLegacy:
        return "BTC Legacy (BIP44)";
    case keyd::DerivePreset::BtcNestedSegwit:
        return "BTC Nested SegWit (BIP49)";
    case keyd::DerivePreset::BtcSegwit:
        return "BTC SegWit (BIP84)";
    case keyd::DerivePreset::BtcTaproot:
        return "BTC Taproot (BIP86)";
    case keyd::DerivePreset::SolPhantom:
        return "Solana (Phantom)";
    }
    return "?";
}

std::span<const keyd::DerivePreset> presets_for(crypto::SecretKind kind)
{
    using P = keyd::DerivePreset;
    static constexpr P kMnemonic[]
        = { P::MetaMask, P::LedgerLive, P::LegacyMew, P::BtcLegacy,
              P::BtcNestedSegwit, P::BtcSegwit, P::BtcTaproot, P::SolPhantom };
    static constexpr P kRawKey[] = { P::MetaMask, P::BtcLegacy,
        P::BtcNestedSegwit, P::BtcSegwit, P::BtcTaproot };
    static constexpr P kWif[]
        = { P::BtcLegacy, P::BtcNestedSegwit, P::BtcSegwit, P::BtcTaproot };
    static constexpr P kSolKey[] = { P::SolPhantom };
    switch (kind) {
    case crypto::SecretKind::Mnemonic:
        return kMnemonic;
    case crypto::SecretKind::RawKey:
        return kRawKey;
    case crypto::SecretKind::Wif:
        return kWif;
    case crypto::SecretKind::SolKey:
        return kSolKey;
    case crypto::SecretKind::EthAddress: // nothing to derive from
    case crypto::SecretKind::BtcAddress:
    case crypto::SecretKind::SolAddress:
    case crypto::SecretKind::Unrecognized:
        break;
    }
    return {};
}

keyd::DerivePreset default_preset(crypto::SecretKind kind)
{
    // A WIF's home format is native segwit; everything else starts on
    // the first preset it is offered.
    if (kind == crypto::SecretKind::Wif)
        return keyd::DerivePreset::BtcSegwit;
    const std::span<const keyd::DerivePreset> offered = presets_for(kind);
    return offered.empty() ? keyd::DerivePreset::MetaMask : offered.front();
}

}
