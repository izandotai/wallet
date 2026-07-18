#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "core/secure/secure_bytes.hpp"
#include "keyd/client.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/pages/vault/accounts_view.hpp"
#include "ui/pages/vault/create_view.hpp"
#include "ui/pages/vault/import_view.hpp"
#include "ui/pages/vault/secret_view.hpp"
#include "ui/pages/vault/unlock_view.hpp"
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
    VaultPage(std::filesystem::path wallets_dir, std::string exe_path,
        std::string initial_active);
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

    // The live trust-plane handle, for pages that submit and approve
    // proposals; null until a keyd has been spawned. Ownership stays
    // with the session — pages borrow, never keep.
    keyd::KeydClient* keyd()
    {
        return m_session.client();
    }

private:
    enum class Mode {
        NoWallets,
        CreateForm,
        ImportForm,
        ShowSecret, // after create or backup
        Locked,
        Unlocked,
    };

    struct Job {
        std::atomic<int> phase { 0 }; // 0 = running, 1 = ok, 2 = failed
        std::string error;            // written before phase goes to 2
        secure::SecureBytes secret;   // backup text when revealing
        keyd::RevealKind secret_kind = keyd::RevealKind::SeedEntropy;
        Mode next = Mode::Locked;     // mode to enter on success
        std::string wallet;           // non-empty: newly created, activate it
    };

    void draw_selector(const i18n::Catalog& tr);
    void poll_job();
    void switch_active(const std::string& id);
    void set_status(const char* key); // i18n key
    void enter(Mode mode);

    void start_create(CreateView::Event ev);
    void start_import(ImportView::Event ev);
    void start_unlock(secure::SecureBytes pass);
    void start_backup(secure::SecureBytes pass);
    void handle_accounts(AccountsView::Event ev);

    WalletStore m_store;
    KeydSession m_session;

    std::string m_active;      // id
    std::string m_active_name; // display
    std::string m_vault_path;  // of the active wallet
    AccountsMeta m_meta;
    Mode m_mode = Mode::NoWallets;

    CreateView m_create;
    ImportView m_import;
    UnlockView m_unlock;
    AccountsView m_accounts;
    SecretView m_secret;

    std::string m_status; // message key or verbatim error
    bool m_status_is_key = false;
    std::shared_ptr<Job> m_job;
    bool m_ime_disabled = false;
    bool m_secret_focus = false; // a secret field is active this frame
};

}
