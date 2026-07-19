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

    // Money moved elsewhere: forget the follow so the next frame
    // re-pulls the ledger.
    void mark_stale()
    {
        m_followed.clear();
    }

private:
    struct Row {
        std::string hash;
        std::string counterparty;
        std::string note;      // "Chain · 07-18 19:02"
        std::string when_hint; // the full moment in the user's clock
        std::string amount;    // "+0.05 ETH", signed
        std::string symbol;    // the moved asset's face for the avatar
        std::string link;      // explorer URL, may be empty
        uint64_t time = 0;     // unix seconds, the merge key
        bool incoming = false;
        bool failed = false;
    };

    // One fetch, one flight per chain — the wall clock is the slowest
    // chain, not the sum of six. Rows land as each chain answers; the
    // page redraws the merge on every landing. One page deep by
    // design: the full book lives on the explorer, linked below the
    // list — the free endpoints' paging proved too flaky to lean on.
    struct Job {
        std::string address;
        int spawned = 0;
        std::atomic<int> pending { 0 };    // chains still flying
        std::atomic<bool> dirty { false }; // fresh rows since last take
        std::mutex mu;                     // guards rows/error/failed
        std::vector<Row> rows;
        std::string error; // first failure — shown only if nothing landed
        int failed = 0;
    };

    // All-chain refresh: evm/btc/sol faces of the identity, empty =
    // that family has nothing to ask. One flight per chain that has
    // an address, whatever the family.
    void refresh(const std::array<std::string, 3>& addrs);

    chains::ChainRegistry m_registry;
    VaultPage& m_vault;
    std::array<std::string, 3> m_addrs {};
    std::string m_followed; // faces joined — the follow key
    double m_fetched_at = 0.0;
    std::vector<Row> m_rows;
    std::shared_ptr<Job> m_job;
    std::string m_status;
};

}
