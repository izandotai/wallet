#include "ui/wallet/import_model.hpp"

#include <cstring>
#include <vector>

#include "core/crypto/bip39.hpp"
#include "ui/wallet/presets.hpp"

namespace izan::ui {

namespace {

    std::string_view trimmed(std::string_view text)
    {
        const auto a = text.find_first_not_of(" \t\r\n");
        if (a == std::string_view::npos)
            return {};
        const auto b = text.find_last_not_of(" \t\r\n");
        return text.substr(a, b - a + 1);
    }

    vault::Wallet wallet_of(
        const crypto::DetectedSecret& hit, std::string_view text)
    {
        vault::Wallet wallet;
        if (hit.kind == crypto::SecretKind::Mnemonic) {
            wallet.entropy = crypto::mnemonic_to_entropy(trimmed(text));
        } else {
            vault::Imported imp;
            imp.label = "imported";
            imp.key = secure::SecureBytes(hit.key.size());
            std::memcpy(imp.key.data(), hit.key.data(), hit.key.size());
            wallet.imported.push_back(std::move(imp));
        }
        return wallet;
    }

}

void ImportModel::update(std::string_view text)
{
    const crypto::DetectedSecret hit = crypto::detect_secret(text);
    m_kind = hit.kind;
    m_previews = {};
    if (!recognized())
        return;
    m_selected = uint8_t(default_preset(m_kind));
    // Address previews, derived right here: the person sees where their
    // money would live before anything touches disk.
    try {
        const vault::Wallet probe = wallet_of(hit, text);
        for (const keyd::DerivePreset p : offered())
            m_previews[uint8_t(p)] = keyd::account_address(probe, 0, p);
    } catch (const std::exception&) {
        // A secret that cannot even address itself is not that kind
        // of secret.
        m_kind = crypto::SecretKind::Unrecognized;
        m_previews = {};
    }
}

void ImportModel::reset()
{
    m_kind = crypto::SecretKind::Unrecognized;
    m_previews = {};
    m_selected = 0;
}

std::span<const keyd::DerivePreset> ImportModel::offered() const
{
    return presets_for(m_kind);
}

void ImportModel::select(keyd::DerivePreset preset)
{
    for (const keyd::DerivePreset p : offered())
        if (p == preset) {
            m_selected = uint8_t(preset);
            return;
        }
}

std::optional<vault::Wallet> ImportModel::build(std::string_view text) const
{
    const crypto::DetectedSecret hit = crypto::detect_secret(text);
    if (hit.kind == crypto::SecretKind::Unrecognized)
        return std::nullopt;
    return wallet_of(hit, text);
}

void prove_wallet(const vault::Wallet& wallet, keyd::DerivePreset preset)
{
    (void)keyd::account_address(wallet, 0, preset);
    if (keyd::preset_family(preset) != keyd::ChainFamily::Sol) {
        const std::vector<uint8_t> probe { 0x69 };
        (void)keyd::sign_payload(wallet, probe);
    }
}

}
