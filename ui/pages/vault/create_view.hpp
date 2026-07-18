#pragma once

#include <array>
#include <string>

#include "core/secure/secure_bytes.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/wallet/store.hpp"

namespace izan::ui {

// The new-wallet form: name, passphrase, confirmation. Owns its own
// buffers and their wiping; hands the page a validated request exactly
// once per submit.
class CreateView {
public:
    struct Event {
        enum class Type { None, Back, Submit };
        Type type = Type::None;
        const char* err = nullptr; // i18n key when validation failed
        std::string name;
        secure::SecureBytes pass;
    };

    void reset();
    Event draw(const i18n::Catalog& tr, bool busy, bool& secret_focus,
        const WalletStore& store);

private:
    std::array<char, 64> m_name {};
    std::array<char, 256> m_pass {};
    std::array<char, 256> m_confirm {};
};

}
