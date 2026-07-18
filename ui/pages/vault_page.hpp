#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/secure/secure_bytes.hpp"
#include "keyd/client.hpp"
#include "ui/i18n/catalog.hpp"

struct GLFWwindow;

namespace izan::ui {

// The wallet manager: a directory of vault files, one per wallet, with
// one active at a time. Wallets are objects — creating, importing
// (mnemonic or raw private key, told apart by content), switching,
// unlocking, backing up. The active wallet's keyd is the only one
// alive; switching tears it down and the next unlock spawns a fresh
// one against the new vault file.
//
// Red lines, inherited from the first vault UI and §3.1:
// 1. passphrase bytes leave the input buffer into guarded memory and
//    the buffer is wiped the moment a flow consumes it;
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
        return m_unlocked;
    }

    // Name of the active wallet; the host persists it across runs.
    const std::string& active() const
    {
        return m_active;
    }

    // The live trust-plane handle, for pages that submit and approve
    // proposals; null until a keyd has been spawned. Ownership stays
    // here — pages borrow, never keep.
    keyd::KeydClient* keyd()
    {
        return m_keyd ? &*m_keyd : nullptr;
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
    void draw_no_wallets(const i18n::Catalog& tr);
    void draw_create_form(const i18n::Catalog& tr);
    void draw_import_form(const i18n::Catalog& tr);
    void draw_show_secret(const i18n::Catalog& tr);
    void draw_locked(const i18n::Catalog& tr);
    void draw_unlocked(const i18n::Catalog& tr);
    void poll_job();

    void rescan_wallets();
    void switch_active(const std::string& name);
    // A fresh wallet's file name: ASCII letters, digits, dash and
    // underscore only — vault paths travel through narrow-string APIs
    // where anything fancier depends on the system code page.
    bool valid_new_name(std::string_view name) const;
    std::string wallet_path(const std::string& name) const;

    // Moves the buffer contents into guarded memory and wipes it.
    secure::SecureBytes take_secret(std::array<char, 256>& buf);
    void wipe_buffers();
    bool ensure_keyd(); // spawn on demand; false + m_status on failure

    std::filesystem::path m_dir;
    std::string m_exe_path;
    std::vector<std::string> m_wallets;
    std::string m_active;
    std::string m_vault_path;       // of the active wallet
    Mode m_mode = Mode::NoWallets;
    bool m_unlocked = false;
    std::string m_address;          // account #0, fetched at unlock
    std::string m_status;           // last message key or verbatim error
    bool m_status_is_key = false;

    std::array<char, 64> m_name {}; // new-wallet name (not secret)
    std::array<char, 256> m_pass {};
    std::array<char, 256> m_confirm {};
    std::array<char, 1024> m_mnemonic_in {};
    secure::SecureBytes m_secret_show;
    keyd::RevealKind m_show_kind = keyd::RevealKind::SeedEntropy;

    std::optional<keyd::KeydClient> m_keyd;
    std::shared_ptr<Job> m_job;
    bool m_ime_disabled = false;
};

}
