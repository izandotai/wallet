#include "ui/wallet/session.hpp"

namespace izan::ui {

KeydSession::KeydSession(std::string exe_path)
    : m_exe_path(std::move(exe_path))
{
}

KeydSession::~KeydSession()
{
    teardown();
}

bool KeydSession::ensure(const std::string& vault_path, std::string* error)
{
    if (m_client)
        return true;
    try {
        m_client = keyd::KeydClient::spawn(m_exe_path, vault_path);
        return true;
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        return false;
    }
}

void KeydSession::teardown()
{
    if (m_client) {
        m_client->shutdown();
        m_client.reset();
    }
    m_addresses.clear();
    // Books are per-wallet: a stale one would dress the NEXT wallet
    // in the previous wallet.s other-family addresses.
    m_family_book.clear();
    m_unlocked = false;
}

void KeydSession::mark_unlocked(bool unlocked)
{
    m_unlocked = unlocked;
    if (!unlocked) {
        m_addresses.clear();
        m_family_book.clear();
    }
}

uint32_t KeydSession::refresh_addresses(uint32_t count, uint8_t preset)
{
    m_addresses.clear();
    if (!m_client)
        return 0;
    auto first = m_client->address(0, preset);
    if (!first) {
        m_addresses.push_back(m_client->last_error());
        return 1;
    }
    m_addresses.push_back(*first);
    if (m_client->wallet_kind() != keyd::RevealKind::SeedEntropy) {
        // A key wallet has exactly the one address.
        return 1;
    }
    for (uint32_t i = 1; i < count; ++i) {
        auto addr = m_client->address(i, preset);
        m_addresses.push_back(addr ? *addr : m_client->last_error());
    }
    return count;
}

void KeydSession::push_address(uint32_t index, uint8_t preset)
{
    if (!m_client)
        return;
    auto addr = m_client->address(index, preset);
    m_addresses.push_back(addr ? *addr : m_client->last_error());
}

uint32_t KeydSession::refresh_family(
    const std::string& family, uint32_t count, uint8_t preset)
{
    auto& book = m_family_book[family];
    book.clear();
    if (!m_client)
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        auto addr = m_client->address(i, preset);
        book.push_back(addr ? *addr : m_client->last_error());
        if (i == 0 && m_client->wallet_kind() != keyd::RevealKind::SeedEntropy)
            return 1; // a key wallet has exactly the one address
    }
    return count;
}

const std::vector<std::string>& KeydSession::family_addresses(
    const std::string& family) const
{
    static const std::vector<std::string> kEmpty;
    const auto it = m_family_book.find(family);
    return it == m_family_book.end() ? kEmpty : it->second;
}

}
