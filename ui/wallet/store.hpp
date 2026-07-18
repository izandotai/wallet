#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace izan::ui {

// Sidecar beside each vault file (public data): the display name, the
// HD account line — how many accounts the user opened and which is
// selected — and the derivation preset chosen at import.
struct AccountsMeta {
    std::string name;
    uint32_t count = 1;
    uint32_t active = 0;
    uint8_t preset = 0;
};

// A wallet's display name is anything the user likes, in any script;
// its FILE name is a 16-hex id minted once at creation (sha-256 of
// name plus random salt) — filesystem-safe forever, stable across
// renames, collision-free across equal names.
struct WalletEntry {
    std::string id;
    std::string name;
};

// The wallet directory: one .qvlt per wallet with its accounts.json
// sidecar. Pure filesystem and metadata — no keys, no processes; the
// trust plane never reads any of this.
class WalletStore {
public:
    explicit WalletStore(std::filesystem::path dir); // creates the dir

    const std::filesystem::path& dir() const
    {
        return m_dir;
    }

    void rescan();

    const std::vector<WalletEntry>& wallets() const
    {
        return m_wallets;
    }

    bool empty() const
    {
        return m_wallets.empty();
    }

    bool known(const std::string& id) const;
    std::string first_id() const; // empty when the directory is

    std::string vault_path(const std::string& id) const;
    std::filesystem::path meta_path(const std::string& id) const;

    // Reads clamp what they load: a hand-edited sidecar cannot push an
    // out-of-range preset or active index into the UI.
    AccountsMeta read_meta(const std::string& id) const;
    void write_meta(const std::string& id, const AccountsMeta& meta) const;

    // Anything printable up to 48 bytes, no control characters, not a
    // duplicate of a wallet already listed.
    bool valid_new_name(std::string_view display) const;

    static std::string mint_id(std::string_view display);

private:
    std::filesystem::path m_dir;
    std::vector<WalletEntry> m_wallets;
};

}
