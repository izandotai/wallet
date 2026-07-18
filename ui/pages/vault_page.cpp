#include "ui/pages/vault_page.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include <glaze/glaze.hpp>

#include <imgui.h>
#include <sodium.h>

#include "core/crypto/bip39.hpp"
#include "core/secure/vault.hpp"
#include "domain/config/config_trust.hpp"
#include "keyd/signer.hpp"
#include "ui/shell/ime.hpp"

namespace izan::ui {

using secure::SecureBytes;

namespace {

    constexpr ImGuiInputTextFlags kSecretField
        = ImGuiInputTextFlags_Password | ImGuiInputTextFlags_AutoSelectAll;

    // Sidecar beside each vault file (public data): the display name
    // and the HD account line — how many accounts the user opened and
    // which is selected.
    struct AccountsMeta {
        std::string name;
        uint32_t count = 1;
        uint32_t active = 0;
    };

    AccountsMeta read_meta_file(const std::filesystem::path& path)
    {
        AccountsMeta meta;
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return meta;
        std::ostringstream buf;
        buf << f.rdbuf();
        AccountsMeta parsed;
        if (!glz::read<glz::opts { .error_on_unknown_keys = false }>(
                parsed, buf.str()))
            meta = parsed;
        return meta;
    }

    void write_meta_file(
        const std::filesystem::path& path, const AccountsMeta& meta)
    {
        std::string out;
        if (glz::write<glz::opts { .prettify = true }>(meta, out))
            return;
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << out;
    }

    std::string_view trimmed(std::string_view text)
    {
        const auto a = text.find_first_not_of(" \t\r\n");
        if (a == std::string_view::npos)
            return {};
        const auto b = text.find_last_not_of(" \t\r\n");
        return text.substr(a, b - a + 1);
    }

    // A raw secp256k1 key pasted as hex: optional 0x, then exactly 64
    // hex digits, and not the zero scalar. (Whether it is a USABLE key
    // is proven later by actually signing with it before the wallet is
    // allowed to exist.)
    std::optional<SecureBytes> parse_raw_key(std::string_view text)
    {
        std::string_view hex = trimmed(text);
        if (hex.starts_with("0x") || hex.starts_with("0X"))
            hex.remove_prefix(2);
        if (hex.size() != 64)
            return std::nullopt;
        SecureBytes key(32);
        bool nonzero = false;
        for (int i = 0; i < 32; ++i) {
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            const int hi = nib(hex[std::size_t(2 * i)]);
            const int lo = nib(hex[std::size_t(2 * i + 1)]);
            if (hi < 0 || lo < 0)
                return std::nullopt;
            key.data()[i] = uint8_t(hi << 4 | lo);
            nonzero = nonzero || key.data()[i];
        }
        if (!nonzero)
            return std::nullopt;
        return key;
    }

}

VaultPage::VaultPage(std::filesystem::path wallets_dir, std::string exe_path,
    std::string initial_active)
    : m_dir(std::move(wallets_dir))
    , m_exe_path(std::move(exe_path))
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    rescan_wallets();
    if (m_wallets.empty()) {
        m_mode = Mode::NoWallets;
        return;
    }
    const bool known = std::any_of(m_wallets.begin(), m_wallets.end(),
        [&](const WalletEntry& w) { return w.id == initial_active; });
    switch_active(known ? initial_active : m_wallets.front().id);
}

VaultPage::~VaultPage()
{
    wipe_buffers();
    if (m_keyd)
        m_keyd->shutdown();
}

void VaultPage::rescan_wallets()
{
    m_wallets.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
        if (entry.path().extension() != ".qvlt")
            continue;
        const std::string id = entry.path().stem().string();
        AccountsMeta meta = read_meta_file(m_dir / (id + ".accounts.json"));
        // Pre-sidecar wallets (the migrated "main") display their id.
        m_wallets.push_back({ id, meta.name.empty() ? id : meta.name });
    }
    std::sort(m_wallets.begin(), m_wallets.end(),
        [](const WalletEntry& a, const WalletEntry& b) {
            return a.name < b.name;
        });
}

std::string VaultPage::wallet_path(const std::string& id) const
{
    return (m_dir / (id + ".qvlt")).string();
}

std::string VaultPage::new_wallet_id(std::string_view display)
{
    uint8_t salt[8];
    randombytes_buf(salt, sizeof salt);
    std::string seed(display);
    seed.append(reinterpret_cast<const char*>(salt), sizeof salt);
    return config::sha256_hex(seed).substr(0, 16);
}

bool VaultPage::valid_new_name(std::string_view display) const
{
    if (display.empty() || display.size() > 48)
        return false;
    for (const char c : display)
        if (uint8_t(c) < 0x20)
            return false;
    return std::none_of(m_wallets.begin(), m_wallets.end(),
        [&](const WalletEntry& w) { return w.name == display; });
}

void VaultPage::switch_active(const std::string& id)
{
    if (m_keyd) {
        m_keyd->shutdown();
        m_keyd.reset();
    }
    wipe_buffers();
    m_account_addrs.clear();
    m_status.clear();
    m_unlocked = false;
    m_active = id;
    m_vault_path = wallet_path(id);
    load_accounts_meta();
    m_mode = std::filesystem::exists(m_vault_path) ? Mode::Locked
                                                   : Mode::NoWallets;
}

void VaultPage::load_accounts_meta()
{
    const AccountsMeta meta
        = read_meta_file(m_dir / (m_active + ".accounts.json"));
    m_active_name = meta.name.empty() ? m_active : meta.name;
    m_account_count = meta.count > 0 ? meta.count : 1;
    m_account_active = meta.active < m_account_count ? meta.active : 0;
}

void VaultPage::save_accounts_meta() const
{
    write_meta_file(m_dir / (m_active + ".accounts.json"),
        { m_active_name, m_account_count, m_account_active });
}

void VaultPage::refresh_addresses()
{
    m_account_addrs.clear();
    if (!m_keyd)
        return;
    auto first = m_keyd->address(0);
    if (!first) {
        m_account_addrs.push_back(m_keyd->last_error());
        return;
    }
    m_account_addrs.push_back(*first);
    if (m_keyd->wallet_kind() != keyd::RevealKind::SeedEntropy) {
        // A key wallet has exactly the one address.
        m_account_count = 1;
        m_account_active = 0;
        return;
    }
    for (uint32_t i = 1; i < m_account_count; ++i) {
        auto addr = m_keyd->address(i);
        m_account_addrs.push_back(addr ? *addr : m_keyd->last_error());
    }
}

SecureBytes VaultPage::take_secret(std::array<char, 256>& buf)
{
    const std::size_t len = strnlen(buf.data(), buf.size());
    SecureBytes out(len);
    if (len)
        std::memcpy(out.data(), buf.data(), len);
    sodium_memzero(buf.data(), buf.size());
    return out;
}

void VaultPage::wipe_buffers()
{
    sodium_memzero(m_pass.data(), m_pass.size());
    sodium_memzero(m_confirm.data(), m_confirm.size());
    sodium_memzero(m_mnemonic_in.data(), m_mnemonic_in.size());
    m_secret_show.reset();
}

bool VaultPage::ensure_keyd()
{
    if (m_keyd)
        return true;
    try {
        m_keyd = keyd::KeydClient::spawn(m_exe_path, m_vault_path);
        return true;
    } catch (const std::exception& e) {
        m_status = e.what();
        m_status_is_key = false;
        return false;
    }
}

void VaultPage::poll_job()
{
    if (!m_job)
        return;
    const int phase = m_job->phase.load();
    if (phase == 0)
        return;
    if (phase == 1) {
        m_status.clear(); // e.g. the "unlocking…" notice has served
        if (!m_job->wallet.empty()) {
            rescan_wallets();
            switch_active(m_job->wallet);
        }
        m_mode = m_job->next;
        m_unlocked = m_mode == Mode::Unlocked;
        if (m_mode == Mode::ShowSecret) {
            m_secret_show = std::move(m_job->secret);
            m_show_kind = m_job->secret_kind;
        }
        if (m_unlocked && m_keyd) {
            // The addresses, fetched once per unlock: the first thing
            // a person needs from an unlocked wallet.
            refresh_addresses();
        }
    } else if (m_job->error.find("passphrase") != std::string::npos) {
        // Known trust-plane refusals ("bad passphrase", "wrong
        // passphrase or corrupted vault") get translated text; anything
        // unexpected stays verbatim — a raw diagnostic beats a wrong
        // translation.
        m_status = "vault.err.unlock";
        m_status_is_key = true;
    } else {
        m_status = m_job->error;
        m_status_is_key = false;
    }
    m_job.reset();
}

void VaultPage::draw(GLFWwindow* window, const i18n::Catalog& tr)
{
    poll_job();

    ImGui::Begin((std::string(tr("vault.title")) + "###vault-page").c_str());

    m_secret_focus = false; // the secret inputs below re-mark it

    if (m_mode == Mode::Locked || m_mode == Mode::Unlocked)
        draw_selector(tr);

    switch (m_mode) {
    case Mode::NoWallets:
        draw_no_wallets(tr);
        break;
    case Mode::CreateForm:
        draw_create_form(tr);
        break;
    case Mode::ImportForm:
        draw_import_form(tr);
        break;
    case Mode::ShowSecret:
        draw_show_secret(tr);
        break;
    case Mode::Locked:
        draw_locked(tr);
        break;
    case Mode::Unlocked:
        draw_unlocked(tr);
        break;
    }

    if (!m_status.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "%s", m_status_is_key ? tr(m_status.c_str()) : m_status.c_str());
    }

    // Secret fields and the IME cannot coexist: composition strings
    // are cached outside the process. Field-level, not mode-level:
    // only passphrase/secret inputs mark the frame — the wallet name
    // is ordinary text and CJK input must keep working there.
    if (m_secret_focus != m_ime_disabled) {
        set_ime_enabled(window, !m_secret_focus);
        m_ime_disabled = m_secret_focus;
    }

    ImGui::End();
}

void VaultPage::draw_selector(const i18n::Catalog& tr)
{
    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    if (ImGui::BeginCombo(tr("wallet.list"), m_active_name.c_str())) {
        for (const WalletEntry& w : m_wallets) {
            ImGui::PushID(w.id.c_str());
            if (ImGui::Selectable(w.name.c_str(), w.id == m_active)
                && w.id != m_active)
                switch_active(w.id);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("vault.create"))) {
        m_status.clear();
        sodium_memzero(m_name.data(), m_name.size());
        m_mode = Mode::CreateForm;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("vault.import"))) {
        m_status.clear();
        sodium_memzero(m_name.data(), m_name.size());
        m_mode = Mode::ImportForm;
    }
    if (m_unlocked && m_keyd) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
            m_keyd->wallet_kind() == keyd::RevealKind::SeedEntropy
                ? tr("vault.kind.hd")
                : tr("vault.kind.key"));
    }
    ImGui::EndDisabled();
    ImGui::Separator();
}

void VaultPage::draw_no_wallets(const i18n::Catalog& tr)
{
    ImGui::TextDisabled("%s", m_dir.string().c_str());
    ImGui::Spacing();
    if (ImGui::Button(tr("vault.create"))) {
        m_status.clear();
        m_mode = Mode::CreateForm;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("vault.import"))) {
        m_status.clear();
        m_mode = Mode::ImportForm;
    }
}

void VaultPage::draw_create_form(const i18n::Catalog& tr)
{
    ImGui::InputText(tr("wallet.name"), m_name.data(), m_name.size());
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);
    m_secret_focus |= ImGui::IsItemActive();
    ImGui::InputText(tr("vault.passphrase.confirm"), m_confirm.data(),
        m_confirm.size(), kSecretField);
    m_secret_focus |= ImGui::IsItemActive();

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.create"))) {
        const std::string name(
            m_name.data(), strnlen(m_name.data(), m_name.size()));
        const std::size_t plen = strnlen(m_pass.data(), m_pass.size());
        if (!valid_new_name(name)) {
            m_status = "wallet.err.name";
            m_status_is_key = true;
        } else if (plen == 0) {
            m_status = "vault.msg.empty_pass";
            m_status_is_key = true;
        } else if (std::strncmp(m_pass.data(), m_confirm.data(), m_pass.size())
            != 0) {
            m_status = "vault.msg.mismatch";
            m_status_is_key = true;
        } else {
            m_status = "vault.busy.creating";
            m_status_is_key = true;
            SecureBytes pass = take_secret(m_pass);
            sodium_memzero(m_confirm.data(), m_confirm.size());

            const std::string id = new_wallet_id(name);
            auto job = std::make_shared<Job>();
            job->next = Mode::ShowSecret;
            job->wallet = id;
            m_job = job;
            std::thread([job, pass = std::move(pass), name,
                            path = wallet_path(id),
                            metaPath
                            = m_dir / (id + ".accounts.json")]() mutable {
                try {
                    vault::Wallet wallet;
                    wallet.entropy = SecureBytes(16);
                    randombytes_buf(
                        wallet.entropy.data(), wallet.entropy.size());
                    vault::save(path, pass, wallet, vault::kdf_sensitive());
                    write_meta_file(metaPath, { name, 1, 0 });
                    job->secret = crypto::entropy_to_mnemonic(wallet.entropy);
                    job->phase.store(1);
                } catch (const std::exception& e) {
                    job->error = e.what();
                    job->phase.store(2);
                }
            }).detach();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("<##create-back")) {
        wipe_buffers();
        m_status.clear();
        m_mode = m_wallets.empty() ? Mode::NoWallets : Mode::Locked;
    }
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
}

void VaultPage::draw_import_form(const i18n::Catalog& tr)
{
    ImGui::InputText(tr("wallet.name"), m_name.data(), m_name.size());
    ImGui::TextUnformatted(tr("vault.secret_in"));
    ImGui::InputTextMultiline("##secret-in", m_mnemonic_in.data(),
        m_mnemonic_in.size(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4));
    m_secret_focus |= ImGui::IsItemActive();
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);
    m_secret_focus |= ImGui::IsItemActive();
    ImGui::InputText(tr("vault.passphrase.confirm"), m_confirm.data(),
        m_confirm.size(), kSecretField);
    m_secret_focus |= ImGui::IsItemActive();

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.import"))) {
        const std::string name(
            m_name.data(), strnlen(m_name.data(), m_name.size()));
        const std::size_t plen = strnlen(m_pass.data(), m_pass.size());
        const std::string_view text(m_mnemonic_in.data(),
            strnlen(m_mnemonic_in.data(), m_mnemonic_in.size()));

        // Content decides what this is: a valid BIP-39 sentence makes
        // a seed wallet, 64 hex digits make a key wallet, anything
        // else is refused — never guessed at.
        vault::Wallet wallet;
        bool recognized = false;
        if (crypto::mnemonic_valid(text)) {
            wallet.entropy = crypto::mnemonic_to_entropy(text);
            recognized = true;
        } else if (auto key = parse_raw_key(text)) {
            vault::Imported imp;
            imp.label = "imported";
            imp.key = std::move(*key);
            wallet.imported.push_back(std::move(imp));
            recognized = true;
        }

        if (!valid_new_name(name)) {
            m_status = "wallet.err.name";
            m_status_is_key = true;
        } else if (plen == 0) {
            m_status = "vault.msg.empty_pass";
            m_status_is_key = true;
        } else if (std::strncmp(m_pass.data(), m_confirm.data(), m_pass.size())
            != 0) {
            m_status = "vault.msg.mismatch";
            m_status_is_key = true;
        } else if (!recognized) {
            m_status = "vault.err.secret";
            m_status_is_key = true;
        } else {
            m_status = "vault.busy.creating";
            m_status_is_key = true;
            SecureBytes pass = take_secret(m_pass);
            sodium_memzero(m_confirm.data(), m_confirm.size());
            sodium_memzero(m_mnemonic_in.data(), m_mnemonic_in.size());

            const std::string id = new_wallet_id(name);
            auto job = std::make_shared<Job>();
            job->next = Mode::Locked;
            job->wallet = id;
            m_job = job;
            std::thread([job, pass = std::move(pass), name,
                            wallet = std::move(wallet), path = wallet_path(id),
                            metaPath
                            = m_dir / (id + ".accounts.json")]() mutable {
                try {
                    // Prove the wallet can actually sign before it is
                    // allowed to exist — this is where an out-of-range
                    // key or a broken seed fails, loudly and early.
                    const std::vector<uint8_t> probe { 0x69 };
                    (void)keyd::sign_payload(wallet, probe);
                    vault::save(path, pass, wallet, vault::kdf_sensitive());
                    write_meta_file(metaPath, { name, 1, 0 });
                    job->phase.store(1);
                } catch (const std::exception& e) {
                    job->error = e.what();
                    job->phase.store(2);
                }
            }).detach();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("<##import-back")) {
        wipe_buffers();
        m_status.clear();
        m_mode = m_wallets.empty() ? Mode::NoWallets : Mode::Locked;
    }
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
}

void VaultPage::draw_show_secret(const i18n::Catalog& tr)
{
    const bool seed = m_show_kind == keyd::RevealKind::SeedEntropy;
    ImGui::TextWrapped("%s", tr("vault.msg.created"));
    ImGui::Spacing();
    if (!m_secret_show.empty())
        ImGui::TextWrapped(
            "%s", reinterpret_cast<const char*>(m_secret_show.data()));
    ImGui::Spacing();
    ImGui::TextWrapped(
        "%s", seed ? tr("vault.warn.backup") : tr("vault.warn.backup.key"));
    ImGui::Spacing();
    if (ImGui::Button(tr("vault.lock"))) {
        m_secret_show.reset();
        m_status.clear();
        m_mode = Mode::Locked;
    }
}

void VaultPage::draw_locked(const i18n::Catalog& tr)
{
    ImGui::TextDisabled("%s", tr("vault.state.locked"));
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);
    m_secret_focus |= ImGui::IsItemActive();

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.unlock"))) {
        if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            m_status = "vault.msg.empty_pass";
            m_status_is_key = true;
        } else if (ensure_keyd()) {
            m_status = "vault.busy.unlocking";
            m_status_is_key = true;
            SecureBytes pass = take_secret(m_pass);

            auto job = std::make_shared<Job>();
            job->next = Mode::Unlocked;
            m_job = job;
            // The client is untouched by the frame loop while a job is
            // in flight (buttons disabled), so handing the pointer to
            // the worker is single-owner in practice.
            keyd::KeydClient* keyd = &*m_keyd;
            std::thread([job, keyd, pass = std::move(pass)]() mutable {
                if (keyd->unlock(pass)) {
                    job->phase.store(1);
                } else {
                    job->error = keyd->last_error();
                    job->phase.store(2);
                }
            }).detach();
        }
    }
    ImGui::EndDisabled();
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.unlocking"));
}

void VaultPage::draw_unlocked(const i18n::Catalog& tr)
{
    ImGui::TextUnformatted(tr("vault.state.unlocked"));

    // The account line: every derived address, the selected one marked.
    // A key wallet shows its single address and no way to grow — that
    // is what it is.
    const bool hd
        = m_keyd && m_keyd->wallet_kind() == keyd::RevealKind::SeedEntropy;
    ImGui::TextDisabled("%s", tr("vault.address"));
    for (uint32_t i = 0; i < m_account_addrs.size(); ++i) {
        ImGui::PushID(int(i));
        const bool selected = i == m_account_active;
        if (ImGui::RadioButton("##acct", selected) && !selected) {
            m_account_active = i;
            save_accounts_meta();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(m_account_addrs[std::size_t(i)].c_str());
        if (ImGui::IsItemClicked())
            ImGui::SetClipboardText(m_account_addrs[std::size_t(i)].c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("ui.copy"));
        ImGui::PopID();
    }
    if (hd && ImGui::Button(tr("wallet.account.add"))) {
        ++m_account_count;
        save_accounts_meta();
        auto addr = m_keyd->address(m_account_count - 1);
        m_account_addrs.push_back(addr ? *addr : m_keyd->last_error());
    }
    ImGui::Spacing();

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.lock")) && m_keyd) {
        if (m_keyd->lock()) {
            m_unlocked = false;
            m_account_addrs.clear();
            m_status.clear();
            m_mode = Mode::Locked;
        }
    }
    ImGui::SameLine();
    // Backup spends the passphrase again — the field below feeds it.
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);
    m_secret_focus |= ImGui::IsItemActive();
    if (ImGui::Button(tr("vault.backup"))) {
        if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            m_status = "vault.msg.empty_pass";
            m_status_is_key = true;
        } else if (ensure_keyd()) {
            m_status.clear();
            SecureBytes pass = take_secret(m_pass);

            auto job = std::make_shared<Job>();
            job->next = Mode::ShowSecret;
            m_job = job;
            keyd::KeydClient* keyd = &*m_keyd;
            std::thread([job, keyd, pass = std::move(pass)]() mutable {
                auto revealed = keyd->reveal(pass);
                if (!revealed) {
                    job->error = keyd->last_error();
                    job->phase.store(2);
                    return;
                }
                try {
                    job->secret_kind = revealed->kind;
                    if (revealed->kind == keyd::RevealKind::SeedEntropy) {
                        job->secret
                            = crypto::entropy_to_mnemonic(revealed->secret);
                    } else {
                        // Key-only wallet: the backup is the key
                        // itself, shown as hex in guarded memory.
                        SecureBytes hex(2 + revealed->secret.size() * 2 + 1);
                        std::memcpy(hex.data(), "0x", 2);
                        sodium_bin2hex(reinterpret_cast<char*>(hex.data()) + 2,
                            hex.size() - 2, revealed->secret.data(),
                            revealed->secret.size());
                        job->secret = std::move(hex);
                    }
                    job->phase.store(1);
                } catch (const std::exception& e) {
                    job->error = e.what();
                    job->phase.store(2);
                }
            }).detach();
        }
    }
    ImGui::EndDisabled();
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy"));
}

}
