#include "ui/pages/vault/create_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/kit.hpp"
#include "ui/widgets/secret_field.hpp"

namespace izan::ui {

void CreateView::reset()
{
    sodium_memzero(m_name.data(), m_name.size());
    sodium_memzero(m_pass.data(), m_pass.size());
    sodium_memzero(m_confirm.data(), m_confirm.size());
    m_focus_pending = true;
}

CreateView::Event CreateView::draw(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, const WalletStore& store)
{
    Event ev;
    const float em = ImGui::GetFontSize();
    const float col = em * 14.0f;

    kit_title(tr("vault.create"));
    kit_vspace(0.5f);

    kit_group_begin("##create-fields", col + em * 1.2f);
    ImGui::SetNextItemWidth(col);
    if (m_focus_pending && !busy) {
        ImGui::SetKeyboardFocusHere();
        m_focus_pending = false;
    }
    ImGui::InputTextWithHint(
        "##name", tr("wallet.name"), m_name.data(), m_name.size());
    kit_hairline();
    ImGui::SetNextItemWidth(col);
    secret_field("##pass", m_pass, secret_focus, tr("vault.passphrase"));
    ImGui::SetNextItemWidth(col);
    bool submit = secret_field(
        "##confirm", m_confirm, secret_focus, tr("vault.passphrase.confirm"));
    kit_group_end();
    kit_vspace(0.5f);

    ImGui::BeginDisabled(busy);
    if (kit_subtle_button(tr("ui.back"))) {
        reset();
        ev.type = Event::Type::Back;
    }
    ImGui::SameLine();
    submit |= kit_primary_button(tr("vault.create"));
    if (submit) {
        const std::string name(
            m_name.data(), strnlen(m_name.data(), m_name.size()));
        if (!store.valid_new_name(name)) {
            ev.err = "wallet.err.name";
        } else if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            ev.err = "vault.msg.empty_pass";
        } else if (std::strncmp(m_pass.data(), m_confirm.data(), m_pass.size())
            != 0) {
            ev.err = "vault.msg.mismatch";
        } else {
            ev.type = Event::Type::Submit;
            ev.name = name;
            ev.pass = take_secret(m_pass);
            sodium_memzero(m_confirm.data(), m_confirm.size());
        }
    }
    ImGui::EndDisabled();
    if (busy) {
        kit_vspace(0.25f);
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
    }
    return ev;
}

}
