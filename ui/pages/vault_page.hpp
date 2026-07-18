#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/crypto/secret_import.hpp"
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

    // The selected account index within the active wallet (always 0
    // for key wallets); send flows derive and sign as this identity.
    uint32_t active_account() const
    {
        return m_account_active;
    }

    // The active wallet's derivation preset (keyd::DerivePreset value).
    // It rides the proposal envelope so an imported mnemonic keeps
    // speaking its home vendor's dialect.
    uint8_t active_preset() const
    {
        return m_preset;
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
    void enter_import_form();
    void refresh_detect(); // re-classify the pasted secret on change
    void draw_create_form(const i18n::Catalog& tr);
    void draw_import_form(const i18n::Catalog& tr);
    void draw_show_secret(const i18n::Catalog& tr);
    void draw_locked(const i18n::Catalog& tr);
    void draw_unlocked(const i18n::Catalog& tr);
    void poll_job();

    // A wallet's display name is anything the user likes, in any
    // script; its FILE name is a 16-hex id minted once at creation
    // (sha-256 of name plus random salt) — filesystem-safe forever,
    // stable across renames, collision-free across equal names. The
    // display name lives in the accounts sidecar.
    struct WalletEntry {
        std::string id;
        std::string name;
    };

    void rescan_wallets();
    void switch_active(const std::string& id);
    void load_accounts_meta();
    void save_accounts_meta() const;
    void refresh_addresses(); // account list, via keyd, while unlocked
    bool valid_new_name(std::string_view display) const;
    static std::string new_wallet_id(std::string_view display);
    std::string wallet_path(const std::string& id) const;

    // Moves the buffer contents into guarded memory and wipes it.
    secure::SecureBytes take_secret(std::array<char, 256>& buf);
    void wipe_buffers();
    bool ensure_keyd(); // spawn on demand; false + m_status on failure

    std::filesystem::path m_dir;
    std::string m_exe_path;
    std::vector<WalletEntry> m_wallets;
    std::string m_active;      // id
    std::string m_active_name; // display
    std::string m_vault_path;  // of the active wallet
    Mode m_mode = Mode::NoWallets;
    bool m_unlocked = false;
    // The HD account line the user has opened so far: how many, which
    // one is selected, and their addresses (fetched at unlock). Not
    // secret — it lives beside the vault file as <name>.accounts.json
    // so it travels with the wallet.
    uint32_t m_account_count = 1;
    uint32_t m_account_active = 0;
    uint8_t m_preset = 0; // keyd::DerivePreset of the active wallet
    std::vector<std::string> m_account_addrs;
    // Import-form recognition: what the pasted text is, plus one
    // address preview per preset the secret can wear — a mnemonic
    // offers every family, a key offers ETH and the BTC formats, and
    // clicking an address selects its preset.
    crypto::SecretKind m_detect = crypto::SecretKind::Unrecognized;
    std::array<std::string, 8> m_preset_addrs {};
    uint8_t m_import_preset = 0;
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
    bool m_secret_focus = false; // a secret field is active this frame
};

}
