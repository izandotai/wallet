#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "keyd/client.hpp"

namespace izan::ui {

// The active wallet's live trust-plane handle, with its address book:
// one keyd per active wallet, spawned on demand, torn down on switch.
// Headless — pages borrow the client, tests drive the session against
// a real child process.
class KeydSession {
public:
    explicit KeydSession(std::string exe_path);
    ~KeydSession();

    KeydSession(const KeydSession&) = delete;
    KeydSession& operator=(const KeydSession&) = delete;

    // Spawns against the vault file if no child is up. false lands the
    // spawn error in *error.
    bool ensure(const std::string& vault_path, std::string* error);

    // Shutdown and forget — switching wallets, or the page going away.
    void teardown();

    // Borrowed, never kept; null until ensure() has succeeded.
    keyd::KeydClient* client()
    {
        return m_client ? &*m_client : nullptr;
    }

    bool unlocked() const
    {
        return m_unlocked;
    }

    // The page's job reports what the child said; the session keeps the
    // flag and the address book consistent with it.
    void mark_unlocked(bool unlocked);

    // Fetches the account line under the wallet's preset: count
    // addresses for an HD wallet, exactly one for a key wallet.
    // Returns how many the wallet actually has.
    uint32_t refresh_addresses(uint32_t count, uint8_t preset);

    // Derives one more account and appends it (HD wallets only).
    void push_address(uint32_t index, uint8_t preset);

    const std::vector<std::string>& addresses() const
    {
        return m_addresses;
    }

    // The all-chain address book: the same account line under another
    // family's preset. Books live beside the birth-family one and are
    // wiped together on lock — an HD seed's other identities, fetched
    // when a page first looks.
    uint32_t refresh_family(
        const std::string& family, uint32_t count, uint8_t preset);

    // Empty until refresh_family has run for that family.
    const std::vector<std::string>& family_addresses(
        const std::string& family) const;

private:
    std::string m_exe_path;
    std::optional<keyd::KeydClient> m_client;
    std::vector<std::string> m_addresses;
    std::map<std::string, std::vector<std::string>> m_family_book;
    bool m_unlocked = false;
};

}
