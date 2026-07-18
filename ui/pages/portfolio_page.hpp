#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    PortfolioPage(const std::filesystem::path& data_dir, VaultPage& vault);

    void draw(const i18n::Catalog& tr);

    // Touching an asset row means "send this"; the host wires the
    // handler to the send page.
    void on_send(std::function<void(uint64_t, const std::string&)> fn)
    {
        m_on_send = std::move(fn);
    }

private:
    struct Row {
        uint64_t chain_id = 0;
        std::string chain;
        std::string symbol;
        std::string amount; // formatted; empty when !ok
        std::string fiat;   // "$123.45"; empty when unpriced or testnet
        std::string error;
        bool ok = false;
        bool testnet = false;
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        std::string error;
        std::vector<Row> rows;
    };

    void refresh(const std::string& address);

    std::shared_ptr<assets::PortfolioReader> m_reader;
    VaultPage& m_vault;
    std::string m_followed;    // the address the shown rows belong to
    double m_fetched_at = 0.0; // frame clock at the last snapshot
    std::vector<Row> m_rows;
    std::function<void(uint64_t, const std::string&)> m_on_send;
    std::shared_ptr<Job> m_job;
    std::string m_status;
    bool m_status_is_key = false;
    bool m_config_modified = false;
};

}
