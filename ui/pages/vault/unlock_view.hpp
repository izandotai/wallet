#pragma once

#include <array>

#include "core/secure/secure_bytes.hpp"
#include "ui/i18n/catalog.hpp"

namespace izan::ui {

// The locked screen: one passphrase field, one button.
class UnlockView {
public:
    struct Event {
        enum class Type { None, Submit };
        Type type = Type::None;
        const char* err = nullptr;
        secure::SecureBytes pass;
    };

    void reset();
    Event draw(const i18n::Catalog& tr, bool busy, bool& secret_focus);

private:
    std::array<char, 256> m_pass {};
};

}
