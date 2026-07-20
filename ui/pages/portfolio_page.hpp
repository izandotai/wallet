#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/assets/balances.hpp"
#include "domain/assets/portfolio.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/pages/vault_page.hpp"

namespace izan::ui {

// The assets page follows the vault: unlock a wallet and its holdings
// appear, no address to paste. Snapshots run on a background job — a
// slow RPC must never freeze the frame loop — and a chain that fails
// to answer stays visible as an unreadable row, never as a blank.
class PortfolioPage {
public:
    // Loads chains.json / tokens.json from data_dir. A malformed
    // config throws (a wallet must not limp along); a config that
    // merely differs from the shipped defaults loads fine but raises
    // the modified-config warning in the page (§ config trust).
    // user_dir carries tokens.user.json — the person's own additions
    // (memecoins, fresh listings) beyond the shipped basics.
    PortfolioPage(const std::filesystem::path& data_dir,
        const std::filesystem::path& user_dir, VaultPage& vault);

    void draw(const i18n::Catalog& tr);

    // Touching an asset row means "send this"; the host wires the
    // handler to the send page.
    void on_send(std::function<void(
            uint64_t, const std::string&, const std::string&, uint8_t)>
            fn)
    {
        m_on_send = std::move(fn);
    }

    // The row menu's "swap" verb; the host wires it to the swap page.
    void on_swap(std::function<void(uint64_t, const std::string&)> fn)
    {
        m_on_swap = std::move(fn);
    }

    // Money moved elsewhere (a send or swap settled): forget the
    // followed address so the next frame's follow logic re-pulls —
    // the established clean-refresh path, chase gate included.
    void mark_stale()
    {
        m_followed.clear();
    }

private:
    struct Row {
        uint64_t chain_id = 0;
        std::string chain;
        std::string symbol;
        std::string token;   // contract address, empty = native coin
        uint8_t decimals = 18;
        std::string amount;  // display-trimmed; empty when !ok
        double approx = 0.0; // full-precision read, for the fiat line
        std::string fiat;    // "$123.45"; empty when unpriced or testnet
        std::string error;
        bool ok = false;
        bool testnet = false;
        // The row's baggage, packed where the row is born: each
        // family's workshop answers for its own rows and the canvas
        // dispatches without ever asking which family this is. (A
        // family conditional in the draw loop once silently swallowed
        // a parameter — this is the structural cure.)
        bool sendable = false;  // click / menu walk to the send form
        bool swappable = false; // the menu offers the exchange desk
        uint8_t addr_slot = 0;  // which identity face: 0 evm 1 btc 2 sol
        const char* addr_path = "/address/"; // the explorer's dialect
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        std::string address;          // whose rows these are — a wallet switch
                                      // mid-flight must not land the old
                                      // wallet's holdings on the new screen
        std::string error;
        std::vector<Row> rows;
        // The prices the fiat column was written with; priced = a
        // fresh fetch succeeded and the page's cache should adopt it.
        std::unordered_map<std::string, double> prices;
        bool priced = false;
    };

    // All-chain refresh: the active identity's face on each family
    // (evm/btc/sol order, empty = that family has no self here). One
    // worker walks whichever engines have an address to ask about.
    void refresh(const std::array<std::string, 3>& addrs);
    // (Re)loads chains/tokens/user tokens and rebuilds the reader —
    // the constructor's body, shared with the add-token flow so a
    // fresh token shows up without a restart.
    void rebuild_reader();
    void draw_add_token(const i18n::Catalog& tr);
    void remove_user_token(uint64_t chain_id, const std::string& address);

    // The add-token dialog asks the contract itself for symbol and
    // decimals; a hand-typed identity is never trusted.
    struct ProbeJob {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        uint64_t chain_id = 0;
        std::string address;          // checksummed
        std::string error;
        assets::TokenProbe found;
    };

    std::shared_ptr<assets::PortfolioReader> m_reader;
    std::map<uint64_t, std::string> m_explorers; // chain id → base URL
    std::filesystem::path m_data_dir;
    std::filesystem::path m_user_dir;
    std::vector<chains::ChainSpec> m_chains;     // for the add-token picker
    std::vector<assets::TokenSpec> m_known;      // for the duplicate check
    // The user's own additions — the only rows that offer "remove";
    // the shipped set lives under the config digest and stays.
    std::vector<std::pair<uint64_t, std::string>> m_user_tokens;
    VaultPage& m_vault;
    std::string m_followed; // family addresses joined — the follow key
    std::array<std::string, 3> m_addrs {}; // evm/btc/sol faces
    double m_fetched_at = 0.0;             // frame clock at the last snapshot
    // Last known USD prices by coingecko id. The feed is rate-limited
    // and moody; a refresh that cannot price reuses these instead of
    // blanking the dollar column, and fetches are spaced a minute
    // apart out of politeness.
    std::unordered_map<std::string, double> m_prices;
    double m_priced_at = -1.0e9;
    std::vector<Row> m_rows;
    std::function<void(
        uint64_t, const std::string&, const std::string&, uint8_t)>
        m_on_send;
    std::function<void(uint64_t, const std::string&)> m_on_swap;
    std::shared_ptr<Job> m_job;
    std::shared_ptr<ProbeJob> m_probe;
    // The token the remove confirmation is about; armed by the row
    // menu, consumed by the dialog.
    uint64_t m_remove_chain = 0;
    std::string m_remove_token;
    std::string m_remove_symbol;
    bool m_open_remove = false;
    int m_add_chain = 0;          // index into m_chains
    std::array<char, 64> m_add_addr {};
    std::string m_add_status;     // i18n key; empty = quiet
    std::optional<assets::TokenProbe> m_add_preview;
    uint64_t m_preview_chain = 0; // what the preview was probed on
    std::string m_preview_addr;
    std::string m_status;
    bool m_status_is_key = false;
    bool m_config_modified = false;
};

}
