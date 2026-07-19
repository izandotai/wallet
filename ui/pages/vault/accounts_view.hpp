#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/secure/secure_bytes.hpp"
#include "ui/i18n/catalog.hpp"

namespace izan::ui {

// The unlocked wallet: a grouped card of accounts — check mark on the
// selected one, editable note, middle-shortened address that copies on
// click with visible feedback — a derive row for HD wallets, then the
// lock and backup actions. Backup asks for the passphrase in its own
// dialog; Enter submits it.
class AccountsView {
public:
    struct Event {
        enum class Type {
            None,
            Select,
            Add,
            Lock,
            Backup,
            LabelEdit,
            RefreshBalance,
        };
        Type type = Type::None;
        const char* err = nullptr;
        uint32_t index = 0;       // Select / LabelEdit / RefreshBalance
        std::string label;        // LabelEdit
        secure::SecureBytes pass; // Backup
    };

    void reset();

    // Loads the note buffers from the sidecar's labels; the page calls
    // this whenever the account line changes hands (unlock, switch,
    // derive).
    void set_labels(std::span<const std::string> labels, std::size_t count);

    // watch = read-only flavor: the lock and backup verbs stay off
    // the table — there is no vault behind these addresses.

    // balances pairs with addresses by index; an empty string means
    // unknown and shows nothing.
    // family_books: the same account line's addresses on evm/btc/sol
    // (fixed order) for all-chain wallets — the receive QR grows a
    // family switch when any book is filled. Empty books = no switch.
    Event draw(const i18n::Catalog& tr, bool busy, bool& secret_focus,
        std::span<const std::string> addresses,
        std::span<const std::string> balances, uint32_t active, bool hd,
        bool watch = false,
        std::array<std::span<const std::string>, 3> family_books = {});

private:
    int m_qr_family = -1; // -1 = the wallet's own line
    std::array<char, 256> m_pass {};
    std::vector<std::array<char, 48>> m_labels;
    bool m_open_backup = false;
    bool m_focus_backup = false;
    int m_qr_index = -1; // row whose QR dialog is up
    bool m_open_qr = false;
};

}
