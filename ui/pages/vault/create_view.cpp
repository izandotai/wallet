#include <cstdlib>
#include "ui/pages/vault/create_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/kit.hpp"

namespace izan::ui {

void CreateView::reset()
{
    sodium_memzero(m_name.data(), m_name.size());
    sodium_memzero(m_pass.data(), m_pass.size());
    sodium_memzero(m_confirm.data(), m_confirm.size());
    m_err = nullptr;
    m_focus_pending = true;
}

CreateView::Event CreateView::draw_dialog(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, const WalletStore& store)
{
    Event ev;
    bool dismissed = false;
    // IZAN_DIALOG_PROBE2=1：Create 对话框自动开一次（无头取证）。
    {
        static const bool probe2 = std::getenv("IZAN_DIALOG_PROBE2") != nullptr;
        static bool fired2 = false;
        if (probe2 && !fired2 && ImGui::GetFrameCount() >= 6) {
            kit_dialog_open("##create-wallet");
            fired2 = true;
        }
    }
    if (!kit_dialog_begin("##create-wallet", &dismissed))
        return ev;
    if (dismissed)
        reset();

    kit_dialog_header_icon("✳", tr("vault.create"));
    kit_dialog_field_width();
    if (m_focus_pending && !busy) {
        kit_focus_here();
        m_focus_pending = false;
    }
    kit_text_field("##name", tr("wallet.name"), m_name.data(), m_name.size());
    kit_dialog_field_width();
    secret_field("##pass", m_pass, secret_focus, tr("vault.passphrase"));
    kit_dialog_field_width();
    bool submit = secret_field(
        "##confirm", m_confirm, secret_focus, tr("vault.passphrase.confirm"));

    if (m_err) {
        ImGui::PushFont(nullptr, kit_caption_size());
        ImGui::TextColored(kit_danger(), "%s", tr(m_err));
        ImGui::PopFont();
    }

    ImGui::BeginDisabled(busy);
    const int choice = kit_dialog_buttons(tr("ui.cancel"), tr("vault.create"));
    ImGui::EndDisabled();
    submit |= choice == 2;

    if (choice == 1) {
        reset();
        kit_dialog_close();
    } else if (submit && !busy) {
        const std::string name(
            m_name.data(), strnlen(m_name.data(), m_name.size()));
        if (!store.valid_new_name(name)) {
            m_err = "wallet.err.name";
        } else if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            m_err = "vault.msg.empty_pass";
        } else if (std::strncmp(m_pass.data(), m_confirm.data(), m_pass.size())
            != 0) {
            m_err = "vault.msg.mismatch";
        } else {
            ev.type = Event::Type::Submit;
            ev.name = name;
            ev.pass = take_secret(m_pass);
            sodium_memzero(m_confirm.data(), m_confirm.size());
            m_err = nullptr;
            kit_dialog_close();
        }
    }
    kit_dialog_end();
    return ev;
}

}
