#include "ui/pages/vault/create_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/secret_field.hpp"

namespace izan::ui {

void CreateView::reset()
{
    sodium_memzero(m_name.data(), m_name.size());
    sodium_memzero(m_pass.data(), m_pass.size());
    sodium_memzero(m_confirm.data(), m_confirm.size());
}

CreateView::Event CreateView::draw(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, const WalletStore& store)
{
    Event ev;
    ImGui::InputText(tr("wallet.name"), m_name.data(), m_name.size());
    secret_field(tr("vault.passphrase"), m_pass, secret_focus);
    secret_field(tr("vault.passphrase.confirm"), m_confirm, secret_focus);

    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.create"))) {
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
    ImGui::SameLine();
    if (ImGui::Button("<##create-back")) {
        reset();
        ev.type = Event::Type::Back;
    }
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
    return ev;
}

}
