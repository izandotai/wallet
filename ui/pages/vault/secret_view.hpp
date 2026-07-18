#pragma once

#include "core/secure/secure_bytes.hpp"
#include "keyd/protocol.hpp"
#include "ui/i18n/catalog.hpp"

namespace izan::ui {

// The one screen allowed to show a root secret (a fresh wallet's
// phrase, a backup reveal). Owns the guarded bytes and wipes them the
// moment the person leaves.
class SecretView {
public:
    void show(secure::SecureBytes secret, keyd::RevealKind kind);
    void reset();

    // true once the person has acknowledged; the secret is gone by then.
    bool draw(const i18n::Catalog& tr);

private:
    secure::SecureBytes m_secret;
    keyd::RevealKind m_kind = keyd::RevealKind::SeedEntropy;
};

}
