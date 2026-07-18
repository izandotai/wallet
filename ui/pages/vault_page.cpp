#include "ui/pages/vault_page.hpp"

#include <cstring>
#include <thread>
#include <vector>

#include <imgui.h>
#include <sodium.h>

#include "core/crypto/bip39.hpp"
#include "core/secure/vault.hpp"
#include "keyd/signer.hpp"
#include "ui/shell/ime.hpp"
#include "ui/wallet/import_model.hpp"
#include "ui/wallet/presets.hpp"

namespace izan::ui {

using secure::SecureBytes;

VaultPage::VaultPage(std::filesystem::path wallets_dir, std::string exe_path,
    std::string initial_active)
    : m_store(std::move(wallets_dir))
    , m_session(std::move(exe_path))
{
    if (m_store.empty()) {
        m_mode = Mode::NoWallets;
        return;
    }
    switch_active(
        m_store.known(initial_active) ? initial_active : m_store.first_id());
}

VaultPage::~VaultPage() = default;

void VaultPage::switch_active(const std::string& id)
{
    m_session.teardown();
    m_create.reset();
    m_import.reset();
    m_unlock.reset();
    m_accounts.reset();
    m_secret.reset();
    m_status.clear();
    m_active = id;
    m_active_name = id;
    m_vault_path = m_store.vault_path(id);
    m_meta = m_store.read_meta(id);
    if (!m_meta.name.empty())
        m_active_name = m_meta.name;
    m_mode = std::filesystem::exists(m_vault_path) ? Mode::Locked
                                                   : Mode::NoWallets;
}

void VaultPage::set_status(const char* key)
{
    m_status = key;
    m_status_is_key = true;
}

void VaultPage::enter(Mode mode)
{
    m_status.clear();
    m_mode = mode;
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
            m_store.rescan();
            switch_active(m_job->wallet);
        }
        m_mode = m_job->next;
        m_session.mark_unlocked(m_mode == Mode::Unlocked);
        if (m_mode == Mode::ShowSecret)
            m_secret.show(std::move(m_job->secret), m_job->secret_kind);
        if (m_mode == Mode::Unlocked) {
            // The addresses, fetched once per unlock: the first thing
            // a person needs from an unlocked wallet.
            const uint32_t actual
                = m_session.refresh_addresses(m_meta.count, m_meta.preset);
            if (actual != m_meta.count) {
                m_meta.count = actual;
                if (m_meta.active >= actual)
                    m_meta.active = 0;
                m_store.write_meta(m_active, m_meta);
            }
        }
    } else if (m_job->error.find("passphrase") != std::string::npos) {
        // Known trust-plane refusals ("bad passphrase", "wrong
        // passphrase or corrupted vault") get translated text; anything
        // unexpected stays verbatim — a raw diagnostic beats a wrong
        // translation.
        set_status("vault.err.unlock");
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
    const bool busy = m_job != nullptr;

    if (m_mode == Mode::Locked || m_mode == Mode::Unlocked)
        draw_selector(tr);

    switch (m_mode) {
    case Mode::NoWallets:
        ImGui::TextDisabled("%s", m_store.dir().string().c_str());
        ImGui::Spacing();
        if (ImGui::Button(tr("vault.create"))) {
            m_create.reset();
            enter(Mode::CreateForm);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("vault.import"))) {
            m_import.reset();
            enter(Mode::ImportForm);
        }
        break;
    case Mode::CreateForm:
        start_create(m_create.draw(tr, busy, m_secret_focus, m_store));
        break;
    case Mode::ImportForm:
        start_import(m_import.draw(tr, busy, m_secret_focus, m_store));
        break;
    case Mode::ShowSecret:
        if (m_secret.draw(tr))
            enter(Mode::Locked);
        break;
    case Mode::Locked: {
        UnlockView::Event ev = m_unlock.draw(tr, busy, m_secret_focus);
        if (ev.err)
            set_status(ev.err);
        else if (ev.type == UnlockView::Event::Type::Submit)
            start_unlock(std::move(ev.pass));
        break;
    }
    case Mode::Unlocked:
        handle_accounts(m_accounts.draw(tr, busy, m_secret_focus,
            m_session.addresses(), m_meta.active,
            m_session.client()
                && m_session.client()->wallet_kind()
                    == keyd::RevealKind::SeedEntropy,
            m_meta.preset));
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
        for (const WalletEntry& w : m_store.wallets()) {
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
        m_create.reset();
        enter(Mode::CreateForm);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("vault.import"))) {
        m_import.reset();
        enter(Mode::ImportForm);
    }
    if (m_session.unlocked() && m_session.client()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
            m_session.client()->wallet_kind() == keyd::RevealKind::SeedEntropy
                ? tr("vault.kind.hd")
                : tr("vault.kind.key"));
    }
    ImGui::EndDisabled();
    ImGui::Separator();
}

void VaultPage::start_create(CreateView::Event ev)
{
    if (ev.err) {
        set_status(ev.err);
        return;
    }
    if (ev.type == CreateView::Event::Type::Back) {
        enter(m_store.empty() ? Mode::NoWallets : Mode::Locked);
        return;
    }
    if (ev.type != CreateView::Event::Type::Submit)
        return;

    set_status("vault.busy.creating");
    const std::string id = WalletStore::mint_id(ev.name);
    auto job = std::make_shared<Job>();
    job->next = Mode::ShowSecret;
    job->wallet = id;
    m_job = job;
    std::thread([job, pass = std::move(ev.pass), name = std::move(ev.name),
                    path = m_store.vault_path(id), store = &m_store,
                    id]() mutable {
        try {
            vault::Wallet wallet;
            wallet.entropy = SecureBytes(16);
            randombytes_buf(wallet.entropy.data(), wallet.entropy.size());
            vault::save(path, pass, wallet, vault::kdf_sensitive());
            store->write_meta(id, { name, 1, 0, 0 });
            job->secret = crypto::entropy_to_mnemonic(wallet.entropy);
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        }
    }).detach();
}

void VaultPage::start_import(ImportView::Event ev)
{
    if (ev.err) {
        set_status(ev.err);
        return;
    }
    if (ev.type == ImportView::Event::Type::Back) {
        enter(m_store.empty() ? Mode::NoWallets : Mode::Locked);
        return;
    }
    if (ev.type != ImportView::Event::Type::Submit || !ev.wallet)
        return;

    set_status("vault.busy.creating");
    const std::string id = WalletStore::mint_id(ev.name);
    auto job = std::make_shared<Job>();
    job->next = Mode::Locked;
    job->wallet = id;
    m_job = job;
    std::thread([job, pass = std::move(ev.pass), name = std::move(ev.name),
                    wallet = std::move(*ev.wallet), preset = ev.preset,
                    path = m_store.vault_path(id), store = &m_store,
                    id]() mutable {
        try {
            prove_wallet(wallet, keyd::DerivePreset(preset));
            vault::save(path, pass, wallet, vault::kdf_sensitive());
            store->write_meta(id, { name, 1, 0, preset });
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        }
    }).detach();
}

void VaultPage::start_unlock(SecureBytes pass)
{
    std::string spawn_err;
    if (!m_session.ensure(m_vault_path, &spawn_err)) {
        m_status = spawn_err;
        m_status_is_key = false;
        return;
    }
    set_status("vault.busy.unlocking");
    auto job = std::make_shared<Job>();
    job->next = Mode::Unlocked;
    m_job = job;
    // The client is untouched by the frame loop while a job is in
    // flight (buttons disabled), so handing the pointer to the worker
    // is single-owner in practice.
    keyd::KeydClient* keyd = m_session.client();
    std::thread([job, keyd, pass = std::move(pass)]() mutable {
        if (keyd->unlock(pass)) {
            job->phase.store(1);
        } else {
            job->error = keyd->last_error();
            job->phase.store(2);
        }
    }).detach();
}

void VaultPage::start_backup(SecureBytes pass)
{
    std::string spawn_err;
    if (!m_session.ensure(m_vault_path, &spawn_err)) {
        m_status = spawn_err;
        m_status_is_key = false;
        return;
    }
    m_status.clear();
    auto job = std::make_shared<Job>();
    job->next = Mode::ShowSecret;
    m_job = job;
    keyd::KeydClient* keyd = m_session.client();
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
                job->secret = crypto::entropy_to_mnemonic(revealed->secret);
            } else {
                // Key-only wallet: the backup is the key itself, shown
                // as hex in guarded memory.
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

void VaultPage::handle_accounts(AccountsView::Event ev)
{
    switch (ev.type) {
    case AccountsView::Event::Type::Select:
        m_meta.active = ev.index;
        m_store.write_meta(m_active, m_meta);
        break;
    case AccountsView::Event::Type::Add:
        ++m_meta.count;
        m_store.write_meta(m_active, m_meta);
        m_session.push_address(m_meta.count - 1, m_meta.preset);
        break;
    case AccountsView::Event::Type::Lock:
        if (m_session.client() && m_session.client()->lock()) {
            m_session.mark_unlocked(false);
            enter(Mode::Locked);
        }
        break;
    case AccountsView::Event::Type::Backup:
        start_backup(std::move(ev.pass));
        break;
    case AccountsView::Event::Type::None:
        if (ev.err)
            set_status(ev.err);
        break;
    }
}

}
