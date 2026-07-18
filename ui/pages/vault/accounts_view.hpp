#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "core/secure/secure_bytes.hpp"
#include "ui/i18n/catalog.hpp"

namespace izan::ui {

// The unlocked wallet: scheme badge, account line with the selected
// one marked, derive button for HD wallets, lock and backup. Backup
// spends the passphrase again — the field here feeds it.
class AccountsView {
public:
    struct Event {
        enum class Type { None, Select, Add, Lock, Backup };
        Type type = Type::None;
        const char* err = nullptr;
        uint32_t index = 0;       // Select
        secure::SecureBytes pass; // Backup
    };

    void reset();
    Event draw(const i18n::Catalog& tr, bool busy, bool& secret_focus,
        std::span<const std::string> addresses, uint32_t active, bool hd,
        uint8_t preset);

private:
    std::array<char, 256> m_pass {};
};

}
