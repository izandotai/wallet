#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/crypto/secret_import.hpp"
#include "core/secure/vault.hpp"
#include "keyd/signer.hpp"

namespace izan::ui {

// The import wizard's brain, headless: classify the pasted text, offer
// the presets it can wear with one first-address preview each, keep the
// selection inside the offered set, and build the wallet the person
// confirmed. Views draw it; tests drive it without a window.
class ImportModel {
public:
    // Re-classify after the text changed — a paste, not every frame.
    // Derives one preview address per offered preset.
    void update(std::string_view text);
    void reset();

    crypto::SecretKind kind() const
    {
        return m_kind;
    }

    bool recognized() const
    {
        return m_kind != crypto::SecretKind::Unrecognized;
    }

    std::span<const keyd::DerivePreset> offered() const;

    const std::string& preview(keyd::DerivePreset preset) const
    {
        return m_previews[uint8_t(preset)];
    }

    uint8_t selected() const
    {
        return m_selected;
    }

    void select(keyd::DerivePreset preset); // ignored if not offered

    // The wallet the current text describes: entropy for a mnemonic, a
    // single imported key otherwise. nullopt when unrecognized.
    std::optional<vault::Wallet> build(std::string_view text) const;

private:
    crypto::SecretKind m_kind = crypto::SecretKind::Unrecognized;
    std::array<std::string, keyd::kDerivePresetCount> m_previews {};
    uint8_t m_selected = 0;
};

// Proves a wallet works before it is allowed to exist: the chosen
// preset must derive an address (an out-of-range key or a broken seed
// fails here, loudly and early), and the secp256k1 material must
// actually sign. Non-EVM presets have no transaction engine yet, so
// their signing probe runs on the default EVM path — same seed, same
// scalar arithmetic. Throws on any failure.
void prove_wallet(const vault::Wallet& wallet, keyd::DerivePreset preset);

}
