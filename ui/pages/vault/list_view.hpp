#pragma once

#include <array>
#include <string>

#include "ui/i18n/catalog.hpp"
#include "ui/wallet/store.hpp"

namespace izan::ui {

// The wallet card list — the workbench's left pane. A pinned header
// holds the door and the filter — a list of a thousand wallets must
// not bury either — with the cards scrolling underneath. One card per
// wallet: name, kind badge, account count, lock dot; the active card
// highlighted. Click activates; the context menu renames or deletes
// (deletion is a modal with the wallet's name typed back).
class WalletListView {
public:
    struct Event {
        enum class Type { None, Activate, Create, Import, Rename, Delete, Pin };
        Type type = Type::None;
        std::string id;
        std::string name; // Rename: the new display name
    };

    Event draw(const i18n::Catalog& tr, bool busy, const WalletStore& store,
        const std::string& active_id, bool active_unlocked);

private:
    std::string m_target; // wallet the open modal talks about
    std::array<char, 64> m_filter {};
    std::array<char, 64> m_rename {};
    std::array<char, 64> m_confirm {};
    bool m_open_rename = false;
    bool m_open_delete = false;
};

}
