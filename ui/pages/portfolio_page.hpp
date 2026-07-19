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
    void on_send(std::function<void(uint64_t, const std::string&)> fn)
    {
        m_on_send = std::move(fn);
    }

    // The row menu's "swap" verb; the host wires it to the swap page.
    void on_swap(std::function<void(uint64_t, const std::string&)> fn)
    {
        m_on_swap = std::move(fn);
    }

private:
    struct Row {
        uint64_t chain_id = 0;
        std::string chain;
        std::string symbol;
        std::string token;   // contract address, empty = native coin
        std::string amount;  // display-trimmed; empty when !ok
        double approx = 0.0; // full-precision read, for the fiat line
        std::string fiat;    // "$123.45"; empty when unpriced or testnet
        std::string error;
        bool ok = false;
        bool testnet = false;
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        std::string address;          // whose rows these are — a wallet switch
                                      // mid-flight must not land the old
                                      // wallet's holdings on the new screen
        std::string error;
        std::vector<Row> rows;
    };

    void refresh(const std::string& address);
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
    std::string m_followed;    // the address the shown rows belong to
    double m_fetched_at = 0.0; // frame clock at the last snapshot
    std::vector<Row> m_rows;
    std::function<void(uint64_t, const std::string&)> m_on_send;
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
