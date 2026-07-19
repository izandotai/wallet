#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "domain/chains/chain_spec.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/pages/vault_page.hpp"

namespace izan::ui {

// The ledger page: the active account's transaction history, pulled
// from each chain's keyless Blockscout instance and merged newest
// first. It follows the vault the way the assets page does — same
// stale-snapshot gate, same swallowed-refresh chase — and a row click
// opens the transaction on its chain's explorer.
class HistoryPage {
public:
    HistoryPage(const std::filesystem::path& data_dir, VaultPage& vault);

    void draw(const i18n::Catalog& tr);

private:
    struct Row {
        std::string hash;
        std::string counterparty;
        std::string note;      // "Chain · 07-18 19:02"
        std::string when_hint; // the full moment in the user's clock
        std::string amount;    // "+0.05 ETH", signed
        std::string link;      // explorer URL, may be empty
        uint64_t time = 0;     // unix seconds, the merge key
        bool incoming = false;
        bool failed = false;
        bool token = false;    // a token transfer row (shell silencing)
    };

    // One fetch, one flight per chain — the wall clock is the slowest
    // chain, not the sum of six. Rows land as each chain answers; the
    // page redraws the merge on every landing. Each job carries one
    // page number; older pages stack under "load older".
    struct Job {
        std::string address;
        int page = 1;
        int spawned = 0;
        std::atomic<int> pending { 0 };    // chains still flying
        std::atomic<bool> dirty { false }; // fresh rows since last take
        std::mutex mu;                     // guards rows/error/failed
        std::vector<Row> rows;             // this page's rows, deduped at birth
        // Snapshots taken at spawn: rows already on screen (by key)
        // and token hashes already seen — page boundaries must not
        // duplicate rows or resurrect silenced native shells.
        std::shared_ptr<const std::set<std::string>> known_keys;
        std::shared_ptr<const std::set<std::string>> known_tokens;
        std::string error; // first failure — shown only if nothing landed
        int failed = 0;
    };

    void refresh(const std::string& address); // page 1, clean slate
    void load_more();                         // the next page, appended
    void fetch_pages(const std::string& address, int page);

    chains::ChainRegistry m_registry;
    VaultPage& m_vault;
    std::string m_followed;
    double m_fetched_at = 0.0;
    std::vector<Row> m_rows;            // displayed: committed + in-flight
    std::vector<Row> m_base;            // committed pages only
    std::set<std::string> m_keys;       // row identity: hash|amount
    std::set<std::string> m_token_seen; // token-row hashes, all pages
    int m_page = 0;                     // deepest committed page
    bool m_more = false;                // the last page brought news
    std::shared_ptr<Job> m_job;
    std::string m_status;
};

}
