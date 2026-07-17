#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace izan::keyd {

// v1 policy skeleton: the auto-approve zone ships welded shut. Every
// proposal pends for a human. The switch exists so the field is
// designed, not bolted on — but flipping it requires paired-client
// credentials (v4 session budgets), never a config default. See
// WALLET-DESIGN.md §3.1 gap two.
struct Policy {
    static constexpr bool kAutoApproveEnabled = false;
};

enum class ProposalState : uint8_t {
    Pending = 0,
    Approved = 1,
    Denied = 2,
    Unknown = 3,
};

// Who submitted: over the open named pipe (any same-user process), or
// carrying a valid session-key MAC (only the process that spawned this
// keyd — the UI). Provenance is displayed at approval time; an
// anonymous proposal is legitimate but must look different from one
// the user's own UI produced.
enum class Provenance : uint8_t {
    Anonymous = 0,
    Ui = 1,
};

struct Proposal {
    uint64_t id = 0;
    Provenance provenance = Provenance::Anonymous;
    ProposalState state = ProposalState::Pending;
    std::vector<uint8_t> payload;
};

// Thread-shared between the proposal-pipe thread and the password
// channel loop.
class ProposalQueue {
public:
    uint64_t submit(std::vector<uint8_t> payload, Provenance provenance)
    {
        std::lock_guard lock(m_mutex);
        Proposal p;
        p.id = ++m_next_id;
        p.provenance = provenance;
        p.payload = std::move(payload);
        m_items.push_back(std::move(p));
        return m_items.back().id;
    }

    ProposalState state(uint64_t id) const
    {
        std::lock_guard lock(m_mutex);
        for (const Proposal& p : m_items)
            if (p.id == id)
                return p.state;
        return ProposalState::Unknown;
    }

    // Only a pending proposal can be resolved, exactly once.
    bool resolve(uint64_t id, ProposalState verdict)
    {
        std::lock_guard lock(m_mutex);
        for (Proposal& p : m_items)
            if (p.id == id && p.state == ProposalState::Pending) {
                p.state = verdict;
                return true;
            }
        return false;
    }

    std::vector<uint64_t> pending_ids() const
    {
        std::lock_guard lock(m_mutex);
        std::vector<uint64_t> out;
        for (const Proposal& p : m_items)
            if (p.state == ProposalState::Pending)
                out.push_back(p.id);
        return out;
    }

    std::optional<Proposal> get(uint64_t id) const
    {
        std::lock_guard lock(m_mutex);
        for (const Proposal& p : m_items)
            if (p.id == id)
                return p;
        return std::nullopt;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Proposal> m_items;
    uint64_t m_next_id = 0;
};

}
