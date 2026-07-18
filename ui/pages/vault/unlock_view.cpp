#include "ui/pages/vault/unlock_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/secret_field.hpp"

namespace izan::ui {

void UnlockView::reset()
{
    sodium_memzero(m_pass.data(), m_pass.size());
}

UnlockView::Event UnlockView::draw(
    const i18n::Catalog& tr, bool busy, bool& secret_focus)
{
    Event ev;
    ImGui::TextDisabled("%s", tr("vault.state.locked"));
    secret_field(tr("vault.passphrase"), m_pass, secret_focus);

    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.unlock"))) {
        if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            ev.err = "vault.msg.empty_pass";
        } else {
            ev.type = Event::Type::Submit;
            ev.pass = take_secret(m_pass);
        }
    }
    ImGui::EndDisabled();
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.unlocking"));
    return ev;
}

}
