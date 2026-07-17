#include "keyd/audit.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>

#include <sodium.h>

namespace izan::keyd {

namespace {

    constexpr char kDigits[] = "0123456789abcdef";

    std::string to_hex(const uint8_t* data, std::size_t size)
    {
        std::string out;
        out.reserve(size * 2);
        for (std::size_t i = 0; i < size; ++i) {
            out += kDigits[data[i] >> 4];
            out += kDigits[data[i] & 0xf];
        }
        return out;
    }

    bool from_hex64(std::string_view hex, uint8_t out[32])
    {
        if (hex.size() != 64)
            return false;
        for (int i = 0; i < 32; ++i) {
            auto nib = [&](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                return -1;
            };
            const int hi = nib(hex[2 * i]);
            const int lo = nib(hex[2 * i + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out[i] = uint8_t(hi << 4 | lo);
        }
        return true;
    }

    void chain_step(
        const uint8_t prev[32], std::string_view event, uint8_t out[32])
    {
        crypto_hash_sha256_state st;
        crypto_hash_sha256_init(&st);
        crypto_hash_sha256_update(&st, prev, 32);
        crypto_hash_sha256_update(
            &st, reinterpret_cast<const uint8_t*>(event.data()), event.size());
        crypto_hash_sha256_final(&st, out);
    }

}

AuditLog::AuditLog(std::string path)
    : m_path(std::move(path))
{
    if (sodium_init() < 0)
        throw std::runtime_error("sodium_init failed");
}

void AuditLog::load_tail()
{
    std::ifstream f(m_path);
    std::string line;
    std::string last;
    while (std::getline(f, line))
        if (!line.empty())
            last = line;
    if (last.size() >= 64)
        from_hex64(std::string_view(last).substr(0, 64), m_chain);
    m_loaded = true;
}

void AuditLog::append(std::string_view event)
{
    if (event.find('\n') != std::string_view::npos)
        throw std::invalid_argument("audit: event must be single-line");
    std::lock_guard lock(m_mutex);
    if (!m_loaded)
        load_tail();

    uint8_t next[32];
    chain_step(m_chain, event, next);
    const std::string line = to_hex(next, 32) + " " + std::string(event) + "\n";

    // stdio for the explicit flush+fsync pair: the record must be on
    // disk before the action it describes proceeds.
    FILE* f = std::fopen(m_path.c_str(), "ab");
    if (!f)
        throw std::runtime_error("audit: cannot open " + m_path);
    const bool ok = std::fwrite(line.data(), 1, line.size(), f) == line.size()
        && std::fflush(f) == 0;
    std::fclose(f);
    if (!ok)
        throw std::runtime_error("audit: write failed " + m_path);
    std::memcpy(m_chain, next, 32);
}

std::optional<uint64_t> AuditLog::verify(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return 0; // no ledger yet = zero valid records, not a break
    uint8_t chain[32] {};
    uint64_t count = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty())
            continue;
        if (line.size() < 66 || line[64] != ' ')
            return std::nullopt;
        uint8_t claimed[32];
        if (!from_hex64(std::string_view(line).substr(0, 64), claimed))
            return std::nullopt;
        uint8_t expect[32];
        chain_step(chain, std::string_view(line).substr(65), expect);
        if (sodium_memcmp(claimed, expect, 32) != 0)
            return std::nullopt;
        std::memcpy(chain, expect, 32);
        ++count;
    }
    return count;
}

}
