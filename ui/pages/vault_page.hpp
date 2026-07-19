#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/secure/secure_bytes.hpp"
#include "keyd/client.hpp"
#include "keyd/signer.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/pages/vault/accounts_view.hpp"
#include "ui/pages/vault/create_view.hpp"
#include "ui/pages/vault/import_view.hpp"
#include "ui/pages/vault/list_view.hpp"
#include "ui/pages/vault/secret_view.hpp"
#include "ui/pages/vault/unlock_view.hpp"
#include "ui/wallet/presets.hpp"
#include "ui/wallet/session.hpp"
#include "ui/wallet/store.hpp"

struct GLFWwindow;

namespace izan::ui {

// The wallet manager page, reduced to an orchestrator: the WalletStore
// knows the directory, the KeydSession owns the trust-plane handle,
// the ImportModel does the import thinking, and each screen is its own
// view component with its own buffers. This class routes their events,
// runs the slow work on background jobs, and keeps the mode machine.
//
// Red lines, inherited from the first vault UI and §3.1:
// 1. passphrase bytes leave input buffers into guarded memory and the
//    buffer is wiped the moment a flow consumes it (views enforce it);
// 2. the IME is physically detached from the window while a secret
//    field has focus — composition strings get cached by the system;
// 3. everything slow (argon2, keyd round-trips) runs on a background
//    job the frame loop polls, never blocks on;
// 4. unlocking goes through keyd: this process never derives the vault
//    key, and backup re-presents the passphrase (Op::Reveal).
class VaultPage {
public:
    VaultPage(std::filesystem::path wallets_dir, std::filesystem::path data_dir,
        std::string exe_path, std::string initial_active);
    ~VaultPage();

    void draw(GLFWwindow* window, const i18n::Catalog& tr);

    bool unlocked() const
    {
        return m_session.unlocked();
    }

    // Name of the active wallet; the host persists it across runs.
    const std::string& active() const
    {
        return m_active;
    }

    // Display name of the active wallet, for pages that name the sender.
    const std::string& active_name() const
    {
        return m_active_name;
    }

    // The selected account's address while unlocked (or watched);
    // empty otherwise. The send and assets pages follow it.
    std::string active_address() const
    {
        const auto& book
            = m_mode == Mode::Watch ? m_meta.watch : m_session.addresses();
        return m_meta.active < book.size() ? book[m_meta.active]
                                           : std::string();
    }

    // True while the active wallet is watch-only: readable everywhere,
    // spendable nowhere — there is no key and never was.
    bool watching() const
    {
        return m_mode == Mode::Watch;
    }

    // The address the read-only pages (assets, history) follow: the
    // active account when unlocked or watching, nothing when locked.
    std::string followed_address() const
    {
        return unlocked() || watching() ? active_address() : std::string();
    }

    // The selected account index within the active wallet (always 0
    // for key wallets); send flows derive and sign as this identity.
    uint32_t active_account() const
    {
        return m_meta.active;
    }

    // The active wallet's derivation preset (keyd::DerivePreset value).
    // It rides the proposal envelope so an imported mnemonic keeps
    // speaking its home vendor's dialect.
    uint8_t active_preset() const
    {
        return m_meta.preset;
    }

    // The active account's address on a given family ("evm"/"btc"/
    // "sol") — the all-chain face of one identity. Empty when locked,
    // or when the wallet has no self on that family (key and watch
    // wallets stay on their own).
    std::string family_address(std::string_view family) const
    {
        if (m_mode == Mode::Watch)
            return family == m_meta.watch_family ? active_address()
                                                 : std::string();
        if (m_mode != Mode::Unlocked)
            return {};
        if (family
            == family_key(
                keyd::preset_family(keyd::DerivePreset(m_meta.preset))))
            return active_address();
        const auto& book = m_session.family_addresses(std::string(family));
        return m_meta.active < book.size() ? book[m_meta.active]
                                           : std::string();
    }

    // The chain family the active addresses live on — the read-only
    // pages route their balance and ledger engines by this. Watch
    // wallets carry their family in the sidecar.
    keyd::ChainFamily active_family() const
    {
        if (m_mode == Mode::Watch) {
            if (m_meta.watch_family == "btc")
                return keyd::ChainFamily::Btc;
            if (m_meta.watch_family == "sol")
                return keyd::ChainFamily::Sol;
            return keyd::ChainFamily::Eth;
        }
        return keyd::preset_family(keyd::DerivePreset(m_meta.preset));
    }

    // The live trust-plane handle, for pages that submit and approve
    // proposals; null until a keyd has been spawned. Ownership stays
    // with the session — pages borrow, never keep.
    keyd::KeydClient* keyd()
    {
        return m_session.client();
    }

    // Menu-bar verbs: the File menu speaks the same events the wallet
    // list and account views do. No-ops while a job is in flight —
    // the single-driver gate holds for every entrance.
    void request_create()
    {
        if (m_job)
            return;
        m_create.reset();
        m_open_create = true;
    }

    void request_import()
    {
        if (m_job)
            return;
        m_import.reset();
        enter(Mode::ImportForm);
    }

    void request_lock()
    {
        if (m_job)
            return;
        if (m_session.client() && m_session.client()->lock()) {
            m_session.mark_unlocked(false);
            enter(Mode::Locked);
        }
    }

private:
    // Create lives in a dialog and the root-secret reveal in another;
    // the mode machine only tracks what fills the detail window.
    enum class Mode {
        NoWallets,
        ImportForm,
        Locked,
        Unlocked,
        Watch, // watch-only wallet on stage: no vault, no keyd
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 = running, 1 = ok, 2 = failed
        std::string error;            // written before phase goes to 2
        secure::SecureBytes secret;   // backup text when revealing
        keyd::RevealKind secret_kind = keyd::RevealKind::SeedEntropy;
        Mode next = Mode::Locked;     // mode to enter on success
        std::string wallet;           // non-empty: newly created, activate it
    };

    void poll_job();
    void switch_active(const std::string& id);
    void set_status(const char* key); // i18n key
    void enter(Mode mode);

    void start_create(CreateView::Event ev);
    void start_import(ImportView::Event ev);
    void start_unlock(secure::SecureBytes pass);
    void start_backup(secure::SecureBytes pass);
    void handle_accounts(AccountsView::Event ev);
    void handle_list(WalletListView::Event ev);
    // Async native balances for the account line; only_index >= 0
    // refreshes that one row and leaves the rest alone.
    void fetch_balances(int only_index = -1);

    WalletStore m_store;
    KeydSession m_session;

    std::string m_active;      // id
    std::string m_active_name; // display
    std::string m_vault_path;  // of the active wallet
    AccountsMeta m_meta;
    Mode m_mode = Mode::NoWallets;

    WalletListView m_list;
    CreateView m_create;
    ImportView m_import;
    UnlockView m_unlock;
    AccountsView m_accounts;
    SecretView m_secret;

    std::string m_status;               // message key or verbatim error
    bool m_status_is_key = false;
    std::optional<Mode> m_pending_mode; // applied at the next frame's top
    bool m_open_create = false;         // arm the create dialog
    std::shared_ptr<Job> m_job;

    // Native balances for the account line, fetched in the background
    // after unlock (EVM wallets, the registry's first chain). Display
    // garnish — an empty string shows nothing. index >= 0 marks a
    // single-row refresh: deriving one address or clicking one balance
    // must not recompute the whole book.
    struct BalanceJob {
        std::atomic<int> phase { 0 };
        int index = -1;
        std::vector<std::string> out;
    };

    std::filesystem::path m_data_dir;
    std::vector<std::string> m_balances;
    std::shared_ptr<BalanceJob> m_bal_job;
    bool m_ime_disabled = false;
    bool m_secret_focus = false; // a secret field is active this frame
};

}
