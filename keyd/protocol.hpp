#pragma once

#include <cstddef>
#include <cstdint>

namespace izan::keyd {

// Password-channel protocol v1. One opcode byte, then the body. The
// deliberate poverty of this protocol is the point: the trust plane's
// entire remote surface fits on one screen.
enum class Op : uint8_t {
    // requests (UI → keyd)
    Unlock = 0x01, // body: passphrase bytes
    Lock = 0x02,
    Status = 0x03,
    Shutdown = 0x04,
    // Approval re-presents the passphrase (§3.1 gap one): a click
    // inside a compromised UI must not be enough to move money, so the
    // human proves presence with the one secret the UI never stores.
    // Approval and signing are one atomic act — the reply is Signed,
    // over the queue's own copy of the payload, so a compromised UI
    // can neither approve without signing nor swap in different bytes.
    Approve = 0x05, // body: u64-LE proposal id, then passphrase bytes
    Deny = 0x06,    // body: u64-LE proposal id
    Pending = 0x07, // body: empty
    Fetch = 0x08,   // body: u64-LE proposal id
    // Backup shows the human their recovery phrase — the most
    // dangerous read in the system, so it spends the passphrase like
    // an approval does.
    Reveal = 0x09, // body: passphrase bytes

    // replies (keyd → UI)
    Hello = 0x40,       // body: version, hardening bitmask (sent once at start)
    Ok = 0x41,
    Err = 0x42,         // body: utf-8 reason
    State = 0x43,       // body: 0 = locked, 1 = unlocked
    PendingList = 0x44, // body: repeated { u64-LE id, u8 provenance }
    ProposalBody = 0x45, // body: u8 provenance, then payload bytes
    Entropy = 0x46,      // body: BIP-39 entropy bytes (16/32)
    Signed = 0x47,       // body: y_parity(1) || r(32) || s(32)
};

inline constexpr std::size_t kSignedBodyBytes = 65;

// Wrong-passphrase throttle: once this many verifications fail in a
// row, every further attempt stalls (seconds growing with the streak,
// capped) before it is even examined. Lives in the child so restarting
// the UI does not reset the meter.
inline constexpr int kPassThrottleAfter = 3;
inline constexpr int kPassThrottleCapSeconds = 10;

inline constexpr uint8_t kProtocolVersion = 1;

// Hello hardening bitmask: which §3.1 process protections engaged.
inline constexpr uint8_t kHardenedDumps = 1 << 0;    // WER excluded
inline constexpr uint8_t kHardenedDynCode = 1 << 1;  // no dynamic code
inline constexpr uint8_t kHardenedDllSig = 1 << 2;   // MS-signed DLLs only
inline constexpr uint8_t kHardenedDllDirs = 1 << 3;  // system32-only search
inline constexpr uint8_t kHardenedAutoLock = 1 << 4; // lock/suspend wipe

// Proposal-pipe protocol v1, spoken over the open named pipe. Any
// same-user process may connect (agents must find the door), so
// nothing secret ever crosses it and nothing on it can move money:
// submissions only ever land in the pending queue.
enum class PipeOp : uint8_t {
    // requests (client → keyd)
    // body: u8 flag (0 = anonymous, 1 = UI), then for flag 1 a 32-byte
    // crypto_auth MAC over the payload keyed by the spawn session
    // subkey (§3.1 gap two: keyd can tell its own UI from a stranger),
    // then the payload bytes.
    Submit = 0x01,
    Query = 0x02, // body: u64-LE proposal id

    // replies (keyd → client)
    Accepted = 0x41,                                 // body: u64-LE proposal id
    Rejected = 0x42,                                 // body: utf-8 reason
    QueryState = 0x43,                               // body: u8 ProposalState
};

inline constexpr std::size_t kProposalMacBytes = 32; // crypto_auth_BYTES

inline void put_u64le(uint8_t* out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out[i] = uint8_t(v >> (8 * i));
}

inline uint64_t get_u64le(const uint8_t* in)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= uint64_t(in[i]) << (8 * i);
    return v;
}

}
