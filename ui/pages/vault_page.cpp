#include "ui/pages/vault_page.hpp"

#include <cstring>
#include <filesystem>
#include <thread>

#include <imgui.h>
#include <sodium.h>

#include "core/crypto/bip39.hpp"
#include "core/secure/vault.hpp"
#include "ui/shell/ime.hpp"

namespace izan::ui {

using secure::SecureBytes;

namespace {

    constexpr ImGuiInputTextFlags kSecretField
        = ImGuiInputTextFlags_Password | ImGuiInputTextFlags_AutoSelectAll;

}

VaultPage::VaultPage(std::string vault_path, std::string exe_path)
    : m_vault_path(std::move(vault_path))
    , m_exe_path(std::move(exe_path))
{
    m_mode
        = std::filesystem::exists(m_vault_path) ? Mode::Locked : Mode::NoVault;
}

VaultPage::~VaultPage()
{
    wipe_buffers();
    if (m_keyd)
        m_keyd->shutdown();
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
    m_mnemonic_show.reset();
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
        m_mode = m_job->next;
        m_unlocked = m_mode == Mode::Unlocked;
        if (m_mode == Mode::ShowMnemonic)
            m_mnemonic_show = std::move(m_job->secret);
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

    switch (m_mode) {
    case Mode::NoVault:
        draw_no_vault(tr);
        break;
    case Mode::CreateForm:
        draw_create_form(tr);
        break;
    case Mode::ImportForm:
        draw_import_form(tr);
        break;
    case Mode::ShowMnemonic:
        draw_show_mnemonic(tr);
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
    // are cached outside the process. Track whether any secret field
    // is active this frame and detach/reattach on the transition.
    const bool secret_active = ImGui::GetIO().WantTextInput
        && (m_mode == Mode::CreateForm || m_mode == Mode::ImportForm
            || m_mode == Mode::Locked);
    if (secret_active != m_ime_disabled) {
        set_ime_enabled(window, !secret_active);
        m_ime_disabled = secret_active;
    }

    ImGui::End();
}

void VaultPage::draw_no_vault(const i18n::Catalog& tr)
{
    ImGui::TextDisabled("%s", m_vault_path.c_str());
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
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);
    ImGui::InputText(tr("vault.passphrase.confirm"), m_confirm.data(),
        m_confirm.size(), kSecretField);

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.create"))) {
        const std::size_t plen = strnlen(m_pass.data(), m_pass.size());
        if (plen == 0) {
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

            auto job = std::make_shared<Job>();
            job->next = Mode::ShowMnemonic;
            m_job = job;
            std::thread([job, pass = std::move(pass),
                            path = m_vault_path]() mutable {
                try {
                    vault::Wallet wallet;
                    wallet.entropy = SecureBytes(16);
                    randombytes_buf(
                        wallet.entropy.data(), wallet.entropy.size());
                    vault::save(path, pass, wallet, vault::kdf_sensitive());
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
        m_mode = Mode::NoVault;
    }
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
}

void VaultPage::draw_import_form(const i18n::Catalog& tr)
{
    ImGui::TextUnformatted(tr("vault.mnemonic"));
    ImGui::InputTextMultiline("##mnemonic-in", m_mnemonic_in.data(),
        m_mnemonic_in.size(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4));
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);
    ImGui::InputText(tr("vault.passphrase.confirm"), m_confirm.data(),
        m_confirm.size(), kSecretField);

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.import"))) {
        const std::size_t plen = strnlen(m_pass.data(), m_pass.size());
        const std::string_view words(m_mnemonic_in.data(),
            strnlen(m_mnemonic_in.data(), m_mnemonic_in.size()));
        if (plen == 0) {
            m_status = "vault.msg.empty_pass";
            m_status_is_key = true;
        } else if (std::strncmp(m_pass.data(), m_confirm.data(), m_pass.size())
            != 0) {
            m_status = "vault.msg.mismatch";
            m_status_is_key = true;
        } else if (!crypto::mnemonic_valid(words)) {
            m_status = "vault.err.mnemonic";
            m_status_is_key = true;
        } else {
            m_status = "vault.busy.creating";
            m_status_is_key = true;
            SecureBytes pass = take_secret(m_pass);
            sodium_memzero(m_confirm.data(), m_confirm.size());
            SecureBytes entropy = crypto::mnemonic_to_entropy(words);
            sodium_memzero(m_mnemonic_in.data(), m_mnemonic_in.size());

            auto job = std::make_shared<Job>();
            job->next = Mode::Locked;
            m_job = job;
            std::thread([job, pass = std::move(pass),
                            entropy = std::move(entropy),
                            path = m_vault_path]() mutable {
                try {
                    vault::Wallet wallet;
                    wallet.entropy = std::move(entropy);
                    vault::save(path, pass, wallet, vault::kdf_sensitive());
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
        m_mode = Mode::NoVault;
    }
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
}

void VaultPage::draw_show_mnemonic(const i18n::Catalog& tr)
{
    ImGui::TextWrapped("%s", tr("vault.msg.created"));
    ImGui::Spacing();
    if (!m_mnemonic_show.empty())
        ImGui::TextWrapped(
            "%s", reinterpret_cast<const char*>(m_mnemonic_show.data()));
    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr("vault.warn.backup"));
    ImGui::Spacing();
    if (ImGui::Button(tr("vault.lock"))) {
        m_mnemonic_show.reset();
        m_status.clear();
        m_mode = Mode::Locked;
    }
}

void VaultPage::draw_locked(const i18n::Catalog& tr)
{
    ImGui::TextDisabled("%s", tr("vault.state.locked"));
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);

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
    ImGui::Spacing();

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.lock")) && m_keyd) {
        if (m_keyd->lock()) {
            m_unlocked = false;
            m_status.clear();
            m_mode = Mode::Locked;
        }
    }
    ImGui::SameLine();
    // Backup spends the passphrase again — the field below feeds it.
    ImGui::InputText(
        tr("vault.passphrase"), m_pass.data(), m_pass.size(), kSecretField);
    if (ImGui::Button(tr("vault.backup"))) {
        if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            m_status = "vault.msg.empty_pass";
            m_status_is_key = true;
        } else if (ensure_keyd()) {
            m_status.clear();
            SecureBytes pass = take_secret(m_pass);

            auto job = std::make_shared<Job>();
            job->next = Mode::ShowMnemonic;
            m_job = job;
            keyd::KeydClient* keyd = &*m_keyd;
            std::thread([job, keyd, pass = std::move(pass)]() mutable {
                auto entropy = keyd->reveal(pass);
                if (!entropy) {
                    job->error = keyd->last_error();
                    job->phase.store(2);
                    return;
                }
                try {
                    job->secret = crypto::entropy_to_mnemonic(*entropy);
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
        ImGui::TextDisabled("%s", tr("vault.busy.unlocking"));
}

}
