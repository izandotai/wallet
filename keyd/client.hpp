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
    // The wallet's account address; refuses while locked (the refusal
    // lands in last_error()).
    std::optional<std::string> address();

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
    std::optional<ApprovedSignature> approve(
        uint64_t id, const secure::SecureBytes& passphrase);
    bool deny(uint64_t id);
    // Backup: re-presents the passphrase, returns the BIP-39 entropy in
    // guarded memory. nullopt carries the refusal in last_error().
    std::optional<secure::SecureBytes> reveal(
        const secure::SecureBytes& passphrase);

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
    std::unique_ptr<ipc::SecretChannel> m_channel;
    secure::SecureBytes m_mac_key;
    std::string m_pipe_name;
    HelloInfo m_hello;
    std::string m_last_error;
};

}
