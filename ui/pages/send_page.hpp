#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "domain/assets/token_registry.hpp"
#include "domain/chains/chain_spec.hpp"
#include "domain/tx/eip1559.hpp"
#include "domain/tx/txflow.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/pages/vault_page.hpp"

struct GLFWwindow;

namespace izan::ui {

// The send page: the amount is the hero of a centered form, and
// everything after "Review & Send" is one confirmation dialog —
// quote, review, passphrase, delivery and receipt share a single
// window, because confirming and authorizing are the same ritual.
// Everything slow runs on a background job the frame loop polls; the
// passphrase follows the vault page's red lines (guarded memory,
// wiped buffers, IME detached while a secret field has focus). The
// trust-plane handle is borrowed from the vault page — keyd only ever
// signs the queue's own bytes, so nothing on this page can move money
// without the human's passphrase at the approval step.
class SendPage {
public:
    // user_dir carries tokens.user.json, folded into the spendables.
    SendPage(const std::filesystem::path& data_dir,
        const std::filesystem::path& user_dir, VaultPage& vault);
    ~SendPage();

    void draw(GLFWwindow* window, const i18n::Catalog& tr);

    // The assets page hands over a clicked holding: select the
    // matching asset and bring this page forward.
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
        Quoting,    // dialog up, fetching nonce/gas/fees
        Review,     // dialog: figures + passphrase; confirm approves
        Delivering, // approve → sign → broadcast → receipt, one job
        Done,
        Failed,
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 running, 1 ok, 2 failed
        std::atomic<int> step { 0 };  // Delivering: 1 signed, 2 broadcast
        std::string error;
        // quote results
        uint64_t nonce = 0;
        uint64_t gas = 0;
        tx::FeeQuote fees;
        // the Solana quote: what a transfer needs to be encodable and
        // survivable — the blockhash, both balances, the rent floor
        std::array<uint8_t, 32> blockhash {};
        uint64_t balance = 0;
        uint64_t to_balance = 0;
        uint64_t rent = 0;
        // delivery results (hash written before step goes to 2)
        std::string tx_hash;
        uint64_t block = 0;
        bool tx_success = false;
    };

    // One spendable thing: a chain's native coin or one of its
    // configured ERC-20 tokens. Choosing an asset chooses the chain.
    struct Asset {
        int chain = 0;     // index into m_registry.all()
        std::string symbol;
        std::string token; // contract address (checksummed), "" = native
        uint8_t decimals = 18;
    };

    void draw_form(const i18n::Catalog& tr);
    void draw_confirm_dialog(const i18n::Catalog& tr);
    void begin_review();
    void begin_sol_review();
    void confirm_sol_send();
    void confirm_send();
    void cancel_flow();
    void poll_job();
    void reset_to_form();
    const chains::ChainSpec& selected_chain() const;

    const Asset& selected_asset() const
    {
        return m_assets[std::size_t(m_asset_index)];
    }

    chains::ChainRegistry m_registry;
    VaultPage& m_vault;

    Stage m_stage = Stage::Form;
    std::vector<Asset> m_assets;
    int m_asset_index = 0;
    std::array<char, 64> m_to {};
    std::array<char, 32> m_amount {};
    std::array<char, 256> m_pass {};
    bool m_ime_disabled = false;
    bool m_secret_focus = false;
    bool m_focus_pass = false; // focus the passphrase once per entry
    bool m_focus_self = false; // raise this dock window next frame

    // The reviewed draft; immutable once the proposal is submitted —
    // keyd signs the queue's copy of exactly these bytes.
    tx::Eip1559Tx m_tx;
    std::string m_wallet_seen;  // last active wallet id; a switch resets
    std::string m_from;
    uint32_t m_account = 0;     // captured at review, rides the envelope
    uint8_t m_preset = 0;       // derivation preset, captured with the account
    std::string m_to_checked;
    std::string m_amount_label; // "0.05 ETH", captured at review
    bool m_token_send = false;  // fee and amount live in different units
    // The Solana leg of the draft: family chosen by the asset, message
    // encoded at confirm time from the quoted blockhash.
    bool m_sol_send = false;
    uint64_t m_sol_lamports = 0;
    std::array<uint8_t, 32> m_sol_blockhash {};
    uint64_t m_sol_balance = 0;
    uint64_t m_sol_to_balance = 0;
    uint64_t m_sol_rent = 0;
    std::vector<uint8_t> m_sol_msg;
    uint64_t m_proposal = 0;

    std::function<void()> m_on_settled;
    std::string m_status; // key or verbatim error
    bool m_status_is_key = false;
    std::shared_ptr<Job> m_job;
};

}
