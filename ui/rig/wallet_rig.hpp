#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "ui/i18n/catalog.hpp"
#include "ui/pages/history_page.hpp"
#include "ui/pages/portfolio_page.hpp"
#include "ui/pages/send_page.hpp"
#include "ui/pages/swap_page.hpp"
#include "ui/pages/vault_page.hpp"

struct GLFWwindow;

namespace izan::ui {

// The wallet's working middle, one construction for any shell: the
// five pages, their containment (a broken config takes down one pane,
// never the vault) and the wiring between them. A shell owns chrome,
// settings, personalities and the dock host; this rig owns what the
// wallet is — the standalone workbench and any future shell mount the
// same middle.
class WalletRig {
public:
    struct Paths {
        std::filesystem::path data_dir;    // shipped chains/tokens
        std::filesystem::path state_dir;   // per-user mutable state
        std::filesystem::path wallets_dir; // one vault file per wallet
        std::string self_exe;              // keyd spawns this image
    };

    WalletRig(const Paths& paths, const std::string& active_wallet);

    // The wiring lambdas capture this; the rig stays where it is born.
    WalletRig(const WalletRig&) = delete;
    WalletRig& operator=(const WalletRig&) = delete;

    // Draws the five dockable page windows; the shell has already laid
    // a dock host (or a window manager) for them to land in.
    void draw(GLFWwindow* window, const i18n::Catalog& tr);

    VaultPage& vault()
    {
        return vault_;
    }

    bool unlocked() const
    {
        return vault_.unlocked();
    }

    // Name of the active wallet; the shell persists it across runs.
    const std::string& active() const
    {
        return vault_.active();
    }

    // Containment reports: empty means the pane is alive.
    const std::string& portfolio_error() const
    {
        return portfolio_error_;
    }

    const std::string& send_error() const
    {
        return send_error_;
    }

    const std::string& swap_error() const
    {
        return swap_error_;
    }

    const std::string& history_error() const
    {
        return history_error_;
    }

private:
    VaultPage vault_;
    std::optional<PortfolioPage> portfolio_;
    std::optional<SendPage> send_;
    std::optional<SwapPage> swap_;
    std::optional<HistoryPage> history_;
    std::string portfolio_error_;
    std::string send_error_;
    std::string swap_error_;
    std::string history_error_;
};

// The dock arrangements the rig's windows understand, offered by any
// shell that hosts a dockspace. Building one replaces the whole
// arrangement: 0 = workbench, 1 = classic three columns.
void wallet_dock_template(
    unsigned int dockspace_id, float width, float height, int tpl);

// A window added in a newer build floats over a layout saved by an
// older one; adopt it where the templates would have put it. Call once
// when restoring a saved layout, after the windows have submitted.
void wallet_dock_adopt_saved();

}
