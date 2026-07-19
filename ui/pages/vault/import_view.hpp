#pragma once

#include <array>
#include <optional>
#include <string>

#include "core/secure/secure_bytes.hpp"
#include "core/secure/vault.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/wallet/import_model.hpp"
#include "ui/wallet/store.hpp"

namespace izan::ui {

// The import wizard's face, two steps: first the paste box with a live
// recognition line and one address preview per preset the secret can
// wear (picking the address picks the preset), then — only after the
// person has SEEN where their money would live — the name and
// passphrase. The ImportModel does the thinking; this class only draws
// it and owns the buffers.
class ImportView {
public:
    struct Event {
        enum class Type { None, Back, Submit };
        Type type = Type::None;
        const char* err = nullptr; // i18n key when validation failed
        std::string name;
        secure::SecureBytes pass;
        std::optional<vault::Wallet> wallet;
        std::string watch;        // non-empty: a watch-only import, no vault
        std::string watch_family; // its chain family ("evm"/"btc"/"sol")
        uint8_t preset = 0;
    };

    void reset();
    Event draw(const i18n::Catalog& tr, bool busy, bool& secret_focus,
        const WalletStore& store);

private:
    enum class Step { Paste, Confirm };

    ImportModel m_model;
    Step m_step = Step::Paste;
    bool m_focus_pending = true;
    std::array<char, 64> m_name {};
    std::array<char, 1024> m_secret_in {};
    std::array<char, 256> m_pass {};
    std::array<char, 256> m_confirm {};
};

}
