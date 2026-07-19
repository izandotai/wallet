#pragma once

#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "domain/assets/token_registry.hpp"
#include "domain/chains/chain_spec.hpp"
#include "domain/sol/jupiter.hpp"
#include "domain/swap/lifi.hpp"
#include "domain/tx/eip1559.hpp"
#include "domain/tx/txflow.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/pages/vault_page.hpp"

struct GLFWwindow;

namespace izan::ui {

// The swap page: sell one asset for another on the same chain,
// through the LI.FI aggregator, on the send page's rails — one
// confirmation dialog for quote, review, passphrase and delivery,
// keyd signing only the queue's own bytes. An ERC-20 sell that lacks
// allowance signs an exact-amount approval and the swap as one
// reviewed pair: two transactions, ordered by nonce, one passphrase
// entry — the human approves the pair as a whole.
class SwapPage {
public:
    SwapPage(const std::filesystem::path& data_dir,
        const std::filesystem::path& user_dir, VaultPage& vault);
    ~SwapPage();

    void draw(GLFWwindow* window, const i18n::Catalog& tr);

    // The assets page hands over a clicked holding: it becomes the
    // sold asset and this page comes forward.
    void prefill(uint64_t chain_id, const std::string& symbol);

    // Fires once when a delivery settles (confirmed or reverted —
    // either way gas moved); the host refreshes the read-only pages.
    void on_settled(std::function<void()> fn)
    {
        m_on_settled = std::move(fn);
    }

private:
    enum class Stage {
        Form,
        Quoting,    // dialog up: route, allowance, nonce, fees
        Review,     // figures + passphrase; confirm approves the pair
        Delivering, // approve → sign → broadcast → receipt, one job
        Done,
        Failed,
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        std::atomic<int> step { 0 };  // Delivering: 1 broadcast, 2 waiting
        bool requote = false; // a Review-time refresh, not a first quote
        std::string error;
        // quote results
        swap::SwapQuote quote;
        sol::JupQuote jq;    // the Solana lane's quote
        uint8_t out_dec = 0; // the bought mint's on-chain decimals
        bool need_approve = false;
        uint64_t nonce = 0;
        uint64_t approve_gas = 0;
        tx::FeeQuote fees;
        // delivery results (hash written before step goes to 2)
        std::string tx_hash; // the swap's hash — the money mover
        uint64_t block = 0;
        bool tx_success = false;
    };

    struct Asset {
        int chain = 0;
        std::string symbol;
        std::string token; // contract address, "" = native
        uint8_t decimals = 18;
    };

    void draw_form(const i18n::Catalog& tr);
    void draw_search_dialog(const i18n::Catalog& tr);
    void draw_confirm_dialog(const i18n::Catalog& tr);
    void begin_review();
    void start_quote(bool requote); // the route/allowance/nonce/fees job
    void confirm_swap();
    void cancel_flow();
    void poll_job();
    void reset_to_form();
    const chains::ChainSpec& selected_chain() const;
    int first_partner(int sell_index) const; // a same-chain other asset

    const Asset& sell() const
    {
        return m_assets[std::size_t(m_sell_index)];
    }

    const Asset& buy() const
    {
        return m_assets[std::size_t(m_buy_index)];
    }

    chains::ChainRegistry m_registry;
    VaultPage& m_vault;

    Stage m_stage = Stage::Form;
    std::vector<Asset> m_assets;
    int m_sell_index = 0;
    int m_buy_index = 0;

    // The searchable catalog: the aggregator's per-chain token menu,
    // fetched once per chain on first search and cached for the
    // session. Picking a row mints a session-local Asset.
    struct CatalogJob {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        uint64_t chain_id = 0;
        std::vector<swap::TokenListing> list;
        std::string error;
    };

    std::map<uint64_t, std::vector<swap::TokenListing>> m_catalog;
    std::shared_ptr<CatalogJob> m_catalog_job;
    std::array<char, 64> m_search {};
    bool m_open_search = false;
    // The Solana lane: sell SOL, buy any mint by address, routes by
    // Jupiter. The aggregator builds the transaction; keyd verifies
    // the fee payer is this very account before signing.
    bool m_sol_lane = false;
    std::array<char, 64> m_mint {};
    sol::JupQuote m_jq;
    uint8_t m_out_dec = 9;
    std::array<char, 32> m_amount {};
    std::array<char, 256> m_pass {};
    bool m_ime_disabled = false;
    bool m_secret_focus = false;
    bool m_focus_pass = false;
    bool m_focus_self = false; // raise this dock window next frame

    // The reviewed drafts; immutable once proposed — keyd signs the
    // queue's copies of exactly these bytes.
    tx::Eip1559Tx m_tx_approve;
    tx::Eip1559Tx m_tx_swap;
    bool m_need_approve = false;
    swap::SwapQuote m_quote;
    std::string m_wallet_seen;
    std::string m_from;
    uint32_t m_account = 0;
    uint8_t m_preset = 0;
    std::string m_pay_label;     // "0.1 ETH", captured at review
    std::string m_receive_label; // "≥ 185.2 USDC", captured at review
    units::U256 m_quote_amount;  // base units sold; requotes reuse it
    double m_quoted_at = 0.0;    // frame clock of the live quote
    uint64_t m_proposal_approve = 0;
    uint64_t m_proposal_swap = 0;

    std::string m_status;
    bool m_status_is_key = false;
    std::shared_ptr<Job> m_job;
    std::function<void()> m_on_settled;
};

}
