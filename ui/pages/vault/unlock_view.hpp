#pragma once

#include <array>
#include <string>

#include "core/secure/secure_bytes.hpp"
#include "ui/i18n/catalog.hpp"

namespace izan::ui {

// The locked screen, composed like a login: the wallet's avatar and
// name centered, one passphrase field, one obvious button. Enter
// submits; the field grabs focus when the screen appears.
class UnlockView {
public:
    struct Event {
        enum class Type { None, Submit };
        Type type = Type::None;
        const char* err = nullptr;
        secure::SecureBytes pass;
    };

    void reset();
    Event draw(const i18n::Catalog& tr, bool busy, bool& secret_focus,
        const std::string& wallet_name);

private:
    std::array<char, 256> m_pass {};
    bool m_focus_pending = true;
};

}
