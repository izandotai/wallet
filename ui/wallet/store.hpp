#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace izan::ui {

// What kind of root secret a wallet holds, recorded in the sidecar so
// the card list can badge wallets without unlocking them. Display data
// only — the trust plane decides from the vault itself.
inline constexpr const char* kKindHd = "hd";
inline constexpr const char* kKindSecp = "secp";
inline constexpr const char* kKindEd25519 = "ed25519";
inline constexpr const char* kKindWatch = "watch"; // no vault, no keys

// Sidecar beside each vault file (public data): the display name, the
// HD account line — how many accounts the user opened, which is
// selected, and a note per account — plus the derivation preset chosen
// at import and the wallet-kind badge.
struct AccountsMeta {
    std::string name;
    uint32_t count = 1;
    uint32_t active = 0;
    uint8_t preset = 0;
    std::string kind;                // kKind*; empty on legacy sidecars
    std::vector<std::string> labels; // per-account notes, index-aligned
    // Watch-only wallets: the observed addresses, public by nature —
    // a sidecar-only wallet with no vault file at all. watch_family
    // says which chain family the addresses live on ("evm"/"btc"/
    // "sol"); empty on pre-family sidecars and means evm.
    std::vector<std::string> watch;
    std::string watch_family;
};

// A wallet's display name is anything the user likes, in any script;
// its FILE name is a 16-hex id minted once at creation (sha-256 of
// name plus random salt) — filesystem-safe forever, stable across
// renames, collision-free across equal names. Kind and count are the
// sidecar's card-face data, cached at rescan so drawing a list never
// touches the disk.
struct WalletEntry {
    std::string id;
    std::string name;
    std::string kind;
    uint32_t count = 1;
    // The vault file's write time — effectively its birth, since the
    // file only changes on a rekey. The list order rides on it.
    std::filesystem::file_time_type born {};
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

    // Destroys a wallet: the vault file is shredded (overwritten, then
    // deleted), the sidecar and audit ledger removed, the list
    // rescanned. The keys are gone unless a backup exists — callers
    // confirm with the human first.
    void delete_wallet(const std::string& id);

    // A watch-only wallet: sidecar only, no vault ever written. The
    // address arrives validated and normalized by the import layer;
    // family names its chain family in the registry vocabulary.
    std::string create_watch(std::string_view display, std::string_view address,
        std::string_view family);

    static std::string mint_id(std::string_view display);

private:
    std::filesystem::path m_dir;
    std::vector<WalletEntry> m_wallets;
};

}
