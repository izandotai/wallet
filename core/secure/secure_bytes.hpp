#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include <sodium.h>

namespace izan::secure {

// The only legal container for key material. Passphrases, entropy,
// mnemonics, seeds and derived private keys — every intermediate form —
// must live here. Never route them through std::string / std::vector
// (reallocation leaves plaintext copies in dead heap blocks) and never
// hand them to logging or formatting layers.
//
// Backed by libsodium guarded allocations:
//   sodium_malloc  guard pages on both sides (out-of-bounds access
//                  crashes), canary, mlock (kept out of the pagefile,
//                  best-effort out of core dumps)
//   sodium_free    zeroes the buffer before releasing it
//   protect()      flips the pages to no-access for quiet periods; even a
//                  stray pointer inside this process cannot read them.
//                  Open a SecureBytes::Access scope to use the bytes.
class SecureBytes {
public:
    SecureBytes() = default;
    explicit SecureBytes(std::size_t size)
        : m_size(size)
    {
        if (sodium_init() < 0)
            throw std::runtime_error("sodium_init failed");
        if (!m_size)
            return;
        m_ptr = static_cast<uint8_t*>(sodium_malloc(m_size));
        if (!m_ptr)
            throw std::runtime_error("sodium_malloc failed (mlock limit?)");
    }
    ~SecureBytes() { reset(); }

    SecureBytes(const SecureBytes&) = delete;
    SecureBytes& operator=(const SecureBytes&) = delete;
    SecureBytes(SecureBytes&& other) noexcept
        : m_ptr(std::exchange(other.m_ptr, nullptr))
        , m_size(std::exchange(other.m_size, 0))
        , m_guarded(std::exchange(other.m_guarded, false))
    {
    }
    SecureBytes& operator=(SecureBytes&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_ptr = std::exchange(other.m_ptr, nullptr);
            m_size = std::exchange(other.m_size, 0);
            m_guarded = std::exchange(other.m_guarded, false);
        }
        return *this;
    }

    void reset()
    {
        if (m_ptr) {
            if (m_guarded)
                sodium_mprotect_readwrite(m_ptr); // free zeroes, needs write
            sodium_free(m_ptr);
        }
        m_ptr = nullptr;
        m_size = 0;
        m_guarded = false;
    }

    // Lock for a quiet period: pages become inaccessible until the next
    // Access scope opens them again.
    void protect()
    {
        if (m_ptr && !m_guarded) {
            sodium_mprotect_noaccess(m_ptr);
            m_guarded = true;
        }
    }

    // Scoped read-write window over a protected buffer; restores
    // no-access on destruction.
    class Access {
    public:
        explicit Access(SecureBytes& bytes)
            : m_bytes(bytes)
            , m_wasGuarded(bytes.m_guarded)
        {
            if (m_wasGuarded) {
                sodium_mprotect_readwrite(m_bytes.m_ptr);
                m_bytes.m_guarded = false;
            }
        }
        ~Access()
        {
            if (m_wasGuarded)
                m_bytes.protect();
        }
        Access(const Access&) = delete;
        Access& operator=(const Access&) = delete;

    private:
        SecureBytes& m_bytes;
        bool m_wasGuarded;
    };

    uint8_t* data() { return m_ptr; }
    const uint8_t* data() const { return m_ptr; }
    std::size_t size() const { return m_size; }
    bool empty() const { return !m_size; }

    // Constant-time comparison; memcmp on secrets leaks timing.
    bool ct_equal(const SecureBytes& other) const
    {
        if (m_size != other.m_size)
            return false;
        return !m_size || sodium_memcmp(m_ptr, other.m_ptr, m_size) == 0;
    }

private:
    uint8_t* m_ptr = nullptr;
    std::size_t m_size = 0;
    bool m_guarded = false; // pages currently no-access
};

}
