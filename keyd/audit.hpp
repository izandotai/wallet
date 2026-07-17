#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace izan::keyd {

// Append-only audit ledger with a hash chain: every line commits to
// everything before it, so a same-user process that rewrites history
// can no longer produce a file that verifies. The chain makes the
// sign≡allow≡audit identity checkable instead of aspirational.
//
// Line format: <64 hex chain hash> <space> <event text> <newline>,
// chain = SHA-256(previous chain hash bytes || event text), genesis
// previous = 32 zero bytes. Events are single-line ASCII.
class AuditLog {
public:
    explicit AuditLog(std::string path);

    // Appends and fsyncs one event. Throws if the event contains a
    // newline or the write fails — an unrecordable action must not
    // happen silently. Safe to call from the proposal-pipe thread and
    // the password loop concurrently.
    void append(std::string_view event);

    const std::string& path() const
    {
        return m_path;
    }

    // Replays a ledger and checks every link. Returns the number of
    // valid records; nullopt if any link is broken.
    static std::optional<uint64_t> verify(const std::string& path);

private:
    std::mutex m_mutex;
    std::string m_path;
    uint8_t m_chain[32] {};
    bool m_loaded = false;

    void load_tail();
};

}
