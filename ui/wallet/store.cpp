#include "ui/wallet/store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <glaze/glaze.hpp>
#include <sodium.h>

extern "C" {
#include <sha2.h>
}

#include "core/secure/vault.hpp"
#include "keyd/signer.hpp"

namespace izan::ui {

WalletStore::WalletStore(std::filesystem::path dir)
    : m_dir(std::move(dir))
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    rescan();
}

void WalletStore::rescan()
{
    m_wallets.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
        if (entry.path().extension() != ".qvlt")
            continue;
        const std::string id = entry.path().stem().string();
        const AccountsMeta meta = read_meta(id);
        // Pre-sidecar wallets (the migrated "main") display their id.
        m_wallets.push_back({ id, meta.name.empty() ? id : meta.name, meta.kind,
            meta.count, entry.last_write_time(ec) });
    }
    // Watch-only wallets are sidecar-only: a .accounts.json with no
    // vault beside it and the watch kind inside.
    constexpr std::string_view kMetaSuffix = ".accounts.json";
    for (const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
        const std::string file = entry.path().filename().string();
        if (!file.ends_with(kMetaSuffix))
            continue;
        const std::string id = file.substr(0, file.size() - kMetaSuffix.size());
        if (std::filesystem::exists(vault_path(id)))
            continue; // a real vault's sidecar, already listed
        const AccountsMeta meta = read_meta(id);
        if (meta.kind != kKindWatch)
            continue;
        m_wallets.push_back({ id, meta.name.empty() ? id : meta.name, meta.kind,
            meta.count, entry.last_write_time(ec) });
    }
    // Oldest first: the first wallet a person ever made stays at the
    // top and new ones join at the bottom — the order memory expects.
    // Name-sorting scrambled that by byte value: uppercase Latin, then
    // lowercase, then CJK.
    std::sort(m_wallets.begin(), m_wallets.end(),
        [](const WalletEntry& a, const WalletEntry& b) {
            return a.born != b.born ? a.born < b.born : a.name < b.name;
        });
}

bool WalletStore::known(const std::string& id) const
{
    return std::any_of(m_wallets.begin(), m_wallets.end(),
        [&](const WalletEntry& w) { return w.id == id; });
}

std::string WalletStore::first_id() const
{
    return m_wallets.empty() ? std::string() : m_wallets.front().id;
}

std::string WalletStore::vault_path(const std::string& id) const
{
    return (m_dir / (id + ".qvlt")).string();
}

std::filesystem::path WalletStore::meta_path(const std::string& id) const
{
    return m_dir / (id + ".accounts.json");
}

AccountsMeta WalletStore::read_meta(const std::string& id) const
{
    AccountsMeta meta;
    std::ifstream f(meta_path(id), std::ios::binary);
    if (f) {
        std::ostringstream buf;
        buf << f.rdbuf();
        AccountsMeta parsed;
        if (!glz::read<glz::opts { .error_on_unknown_keys = false }>(
                parsed, buf.str()))
            meta = parsed;
    }
    if (meta.kind == kKindWatch)
        meta.count = uint32_t(meta.watch.size());
    if (meta.count == 0)
        meta.count = 1;
    if (meta.active >= meta.count)
        meta.active = 0;
    if (meta.preset >= keyd::kDerivePresetCount)
        meta.preset = 0;
    return meta;
}

void WalletStore::write_meta(
    const std::string& id, const AccountsMeta& meta) const
{
    std::string out;
    if (glz::write<glz::opts { .prettify = true }>(meta, out))
        return;
    std::ofstream f(meta_path(id), std::ios::binary | std::ios::trunc);
    f << out;
}

void WalletStore::delete_wallet(const std::string& id)
{
    if (std::filesystem::exists(vault_path(id)))
        vault::shred(vault_path(id)); // a watch wallet has none to shred
    std::error_code ec;
    std::filesystem::remove(meta_path(id), ec);
    std::filesystem::remove(m_dir / (id + ".qvlt.audit"), ec);
    rescan();
}

std::string WalletStore::create_watch(
    std::string_view display, std::string_view address)
{
    const std::string id = mint_id(display);
    AccountsMeta meta;
    meta.name = display;
    meta.kind = kKindWatch;
    meta.watch.emplace_back(address);
    meta.count = 1;
    write_meta(id, meta);
    rescan();
    return id;
}

bool WalletStore::valid_new_name(std::string_view display) const
{
    if (display.empty() || display.size() > 48)
        return false;
    for (const char c : display)
        if (uint8_t(c) < 0x20)
            return false;
    return std::none_of(m_wallets.begin(), m_wallets.end(),
        [&](const WalletEntry& w) { return w.name == display; });
}

std::string WalletStore::mint_id(std::string_view display)
{
    uint8_t salt[8];
    randombytes_buf(salt, sizeof salt);
    std::string seed(display);
    seed.append(reinterpret_cast<const char*>(salt), sizeof salt);

    uint8_t digest[32];
    sha256_Raw(
        reinterpret_cast<const uint8_t*>(seed.data()), seed.size(), digest);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string id(16, '0');
    for (int i = 0; i < 8; ++i) {
        id[std::size_t(2 * i)] = kHex[digest[i] >> 4];
        id[std::size_t(2 * i + 1)] = kHex[digest[i] & 0xF];
    }
    return id;
}

}
