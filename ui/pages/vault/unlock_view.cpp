#include "ui/pages/vault/unlock_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/kit.hpp"

namespace izan::ui {

void UnlockView::reset()
{
    sodium_memzero(m_pass.data(), m_pass.size());
    m_focus_pending = true;
}

UnlockView::Event UnlockView::draw(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, const std::string& wallet_name)
{
    Event ev;
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float col
        = em * design().form_width < avail ? em * design().form_width : avail;
    const float width = ImGui::GetWindowSize().x;
    auto centered = [&](float w) {
        ImGui::SetCursorPosX((width - w) * 0.5f > 0 ? (width - w) * 0.5f : 0);
    };

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.18f));

    const float avatar = em * design().lock_avatar;
    centered(avatar);
    kit_avatar(wallet_name.c_str(), avatar);
    kit_vspace(0.5f);

    ImGui::PushFont(nullptr, kit_title_size());
    centered(ImGui::CalcTextSize(wallet_name.c_str()).x);
    ImGui::TextUnformatted(wallet_name.c_str());
    ImGui::PopFont();

    ImGui::PushFont(nullptr, kit_caption_size());
    centered(ImGui::CalcTextSize(tr("vault.state.locked")).x);
    ImGui::TextDisabled("%s", tr("vault.state.locked"));
    ImGui::PopFont();
    kit_vspace(0.8f);

    bool submit = false;
    centered(col);
    ImGui::SetNextItemWidth(col);
    if (m_focus_pending && !busy) {
        kit_focus_here();
        m_focus_pending = false;
    }
    submit |= secret_field(
        "##unlock-pass", m_pass, secret_focus, tr("vault.passphrase"));
    kit_vspace(0.25f);

    ImGui::BeginDisabled(busy);
    const float button_w = ImGui::CalcTextSize(tr("vault.unlock")).x
        + ImGui::GetStyle().FramePadding.x * 2.0f;
    centered(button_w);
    submit |= kit_primary_button(tr("vault.unlock"));
    if (submit) {
        if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            ev.err = "vault.msg.empty_pass";
        } else {
            ev.type = Event::Type::Submit;
            ev.pass = take_secret(m_pass);
        }
    }
    ImGui::EndDisabled();
    if (busy) {
        kit_vspace(0.25f);
        centered(ImGui::CalcTextSize(tr("vault.busy.unlocking")).x);
        ImGui::TextDisabled("%s", tr("vault.busy.unlocking"));
    }
    return ev;
}

}
