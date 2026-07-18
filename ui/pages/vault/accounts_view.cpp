#include "ui/pages/vault/accounts_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/wallet/presets.hpp"
#include "ui/widgets/secret_field.hpp"

namespace izan::ui {

void AccountsView::reset()
{
    sodium_memzero(m_pass.data(), m_pass.size());
}

AccountsView::Event AccountsView::draw(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, std::span<const std::string> addresses, uint32_t active,
    bool hd, uint8_t preset)
{
    Event ev;
    ImGui::TextUnformatted(tr("vault.state.unlocked"));

    // The scheme badge: always meaningful for an HD wallet; for a key
    // wallet only when the preset names an address format (a vendor
    // path label would be noise on a wallet that derives nothing).
    if (hd
        || keyd::preset_family(keyd::DerivePreset(preset))
            != keyd::ChainFamily::Eth) {
        ImGui::TextDisabled("%s", tr("wallet.preset"));
        ImGui::SameLine();
        ImGui::TextUnformatted(preset_name(keyd::DerivePreset(preset)));
    }

    ImGui::TextDisabled("%s", tr("vault.address"));
    for (uint32_t i = 0; i < addresses.size(); ++i) {
        ImGui::PushID(int(i));
        const bool selected = i == active;
        if (ImGui::RadioButton("##acct", selected) && !selected) {
            ev.type = Event::Type::Select;
            ev.index = i;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(addresses[std::size_t(i)].c_str());
        if (ImGui::IsItemClicked())
            ImGui::SetClipboardText(addresses[std::size_t(i)].c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("ui.copy"));
        ImGui::PopID();
    }
    if (hd && ImGui::Button(tr("wallet.account.add")))
        ev.type = Event::Type::Add;
    ImGui::Spacing();

    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.lock")))
        ev.type = Event::Type::Lock;
    ImGui::SameLine();
    secret_field(tr("vault.passphrase"), m_pass, secret_focus);
    if (ImGui::Button(tr("vault.backup"))) {
        if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            ev.err = "vault.msg.empty_pass";
        } else {
            ev.type = Event::Type::Backup;
            ev.pass = take_secret(m_pass);
        }
    }
    ImGui::EndDisabled();
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy"));
    return ev;
}

}
