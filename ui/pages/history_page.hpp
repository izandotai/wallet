#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
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
        bool incoming = false;
        bool failed = false;
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        std::string address;
        std::string error;
        std::vector<Row> rows;
    };

    void refresh(const std::string& address);

    chains::ChainRegistry m_registry;
    VaultPage& m_vault;
    std::string m_followed;
    double m_fetched_at = 0.0;
    std::vector<Row> m_rows;
    std::shared_ptr<Job> m_job;
    std::string m_status;
};

}
