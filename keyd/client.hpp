#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/secure/secure_bytes.hpp"
#include "keyd/proposals.hpp"
#include "keyd/protocol.hpp"
#include "platform/ipc/secret_channel.hpp"
#include "platform/proc/child_process.hpp"

namespace izan::keyd {

struct HelloInfo {
    uint8_t version = 0;
    uint8_t hardened = 0; // kHardened* bitmask
};

struct PendingItem {
    uint64_t id = 0;
    Provenance provenance = Provenance::Anonymous;
};

// The Signed reply, unpacked: ready for eip1559::encode_signed.
struct ApprovedSignature {
    uint8_t y_parity = 0;
    std::array<uint8_t, 32> r {};
    std::array<uint8_t, 32> s {};
};

// UI-side handle to a spawned keyd. Owns the process and the password
// channel; destroying the client closes the channel, which the child
// treats as parent death and exits after wiping.
class KeydClient {
public:
    // Spawns `exe --keyd-child` with fresh pipes, a fresh one-shot
    // session key and a fresh proposal pipe name, and consumes the
    // child's Hello. Defaults: audit ledger next to the vault; a
    // pid-plus-random pipe name. Tests override pipe_name to stage
    // squatting.
    static KeydClient spawn(const std::string& exe,
        const std::string& vault_path, const std::string& audit_path = "",
        const std::string& pipe_name = "");

    const HelloInfo& hello() const
    {
        return m_hello;
    }

    const std::string& pipe_name() const
    {
        return m_pipe_name;
    }

    // true on Ok; false carries the child's refusal in last_error().
    bool unlock(const secure::SecureBytes& passphrase);
    bool lock();
    // nullopt = channel broken.
    std::optional<bool> unlocked();
    bool shutdown();
    // A wallet address by account index and derivation preset (HD
    // wallets derive one per index, key wallets only answer for 0);
    // refuses while locked (the refusal lands in last_error()).
    std::optional<std::string> address(
        uint32_t account = 0, uint8_t preset = 0);

    // What the active wallet is, learned from the last successful
    // address() call — drives type-adaptive labels in the UI.
    keyd::RevealKind wallet_kind() const
    {
        return m_wallet_kind;
    }

    // Submits over the proposal pipe with the session-subkey MAC that
    // marks the proposal as coming from this UI (§3.1 gap two).
    // Returns the proposal id.
    std::optional<uint64_t> submit_ui(const std::vector<uint8_t>& payload);

    // Password-channel proposal management. Approval re-presents the
    // passphrase (§3.1 gap one) — there is deliberately no overload
    // without one — and a successful approval IS the signature over
    // the proposal's payload; nullopt carries the refusal in
    // last_error().
    std::optional<std::vector<PendingItem>> pending();
    std::optional<std::pair<Provenance, std::vector<uint8_t>>> fetch(
        uint64_t id);
    // The Solana twin: same request, a 64-byte ed25519 signature back.
    // Callers know their proposal's family from the envelope they
    // built; asking with the wrong twin reads as an error reply.
    std::optional<std::array<uint8_t, 64>> approve_sol(
        uint64_t id, const secure::SecureBytes& passphrase);

    std::optional<ApprovedSignature> approve(
        uint64_t id, const secure::SecureBytes& passphrase);
    bool deny(uint64_t id);

    // Backup: re-presents the passphrase, returns the wallet's root
    // secret in guarded memory — seed entropy for a seed wallet, the
    // raw key for a key-only one. nullopt carries the refusal in
    // last_error().
    struct Revealed {
        RevealKind kind = RevealKind::SeedEntropy;
        secure::SecureBytes secret;
    };

    std::optional<Revealed> reveal(const secure::SecureBytes& passphrase);

    const std::string& last_error() const
    {
        return m_last_error;
    }

    // Exit code if the child ends within the timeout.
    std::optional<uint32_t> wait_exit(uint32_t timeout_ms);
    void drop_channel(); // simulate parent death while keeping the process

private:
    KeydClient() = default;

    std::optional<secure::SecureBytes> request(
        const uint8_t* frame, std::size_t size);

    proc::ChildProcess m_child;
    RevealKind m_wallet_kind = RevealKind::SeedEntropy;
    std::unique_ptr<ipc::SecretChannel> m_channel;
    secure::SecureBytes m_mac_key;
    std::string m_pipe_name;
    HelloInfo m_hello;
    std::string m_last_error;
};

}
