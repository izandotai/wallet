#include "ui/pages/vault/accounts_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/kit.hpp"
#include "ui/widgets/secret_field.hpp"

namespace izan::ui {

namespace {

    // 0x1234567…abcdef — enough of each end to recognize, hover for
    // the whole thing.
    std::string shortened(const std::string& addr)
    {
        if (addr.size() <= 20)
            return addr;
        return addr.substr(0, 10) + "…" + addr.substr(addr.size() - 6);
    }

}

void AccountsView::reset()
{
    sodium_memzero(m_pass.data(), m_pass.size());
    m_labels.clear();
    m_copied = -1;
}

void AccountsView::set_labels(
    std::span<const std::string> labels, std::size_t count)
{
    m_labels.assign(count, {});
    for (std::size_t i = 0; i < count && i < labels.size(); ++i)
        std::memcpy(m_labels[i].data(), labels[i].data(),
            std::min(labels[i].size(), m_labels[i].size() - 1));
}

AccountsView::Event AccountsView::draw(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, std::span<const std::string> addresses, uint32_t active,
    bool hd)
{
    Event ev;
    const float em = ImGui::GetFontSize();

    kit_caption(tr("vault.address"));
    kit_vspace(0.15f);
    if (m_labels.size() < addresses.size())
        m_labels.resize(addresses.size());

    kit_group_begin("##accounts");
    for (uint32_t i = 0; i < addresses.size(); ++i) {
        ImGui::PushID(int(i));
        if (i > 0)
            kit_hairline();

        // Selection mark: a filled accent circle on the active row, a
        // quiet ring elsewhere; clicking it moves the selection.
        const ImVec2 mark_pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##select", ImVec2(em * 1.2f, em * 1.3f))
            && i != active) {
            ev.type = Event::Type::Select;
            ev.index = i;
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 mark(mark_pos.x + em * 0.45f, mark_pos.y + em * 0.65f);
        if (i == active) {
            draw->AddCircleFilled(
                mark, em * 0.3f, ImGui::GetColorU32(kit_accent()));
            draw->AddText(ImGui::GetFont(), kit_caption_size(),
                ImVec2(mark.x - em * 0.17f, mark.y - em * 0.44f),
                IM_COL32(255, 255, 255, 240), "✓");
        } else {
            draw->AddCircle(mark, em * 0.3f,
                ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::SameLine();
        ImGui::PushFont(nullptr, kit_caption_size());
        ImGui::Text("#%u", i);
        ImGui::PopFont();

        // The note edits in place, framelessly — text until touched.
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 7.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::InputTextWithHint("##note", tr("wallet.note"),
            m_labels[i].data(), m_labels[i].size());
        ImGui::PopStyleColor();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ev.type = Event::Type::LabelEdit;
            ev.index = i;
            ev.label = std::string(m_labels[i].data(),
                strnlen(m_labels[i].data(), m_labels[i].size()));
        }

        // Address: middle-shortened, click to copy, and say so.
        ImGui::SameLine();
        const std::string& full = addresses[std::size_t(i)];
        ImGui::TextUnformatted(shortened(full).c_str());
        if (ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(full.c_str());
            m_copied = int(i);
            m_copied_at = ImGui::GetTime();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s\n%s", full.c_str(), tr("ui.copy"));
        }
        if (m_copied == int(i) && ImGui::GetTime() - m_copied_at < 1.6) {
            ImGui::SameLine();
            ImGui::PushFont(nullptr, kit_caption_size());
            ImGui::TextColored(kit_accent(), "✓ %s", tr("ui.copied"));
            ImGui::PopFont();
        }
        ImGui::PopID();
    }
    if (hd) {
        kit_hairline();
        ImGui::PushStyleColor(ImGuiCol_Text, kit_accent());
        if (kit_subtle_button(tr("wallet.account.add")))
            ev.type = Event::Type::Add;
        ImGui::PopStyleColor();
    }
    kit_group_end();

    kit_vspace(0.6f);
    ImGui::BeginDisabled(busy);
    if (kit_subtle_button(tr("vault.lock")))
        ev.type = Event::Type::Lock;
    ImGui::SameLine();
    if (kit_subtle_button(tr("vault.backup"))) {
        sodium_memzero(m_pass.data(), m_pass.size());
        m_open_backup = true;
        m_focus_backup = true;
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("vault.busy"));
    }

    if (m_open_backup) {
        ImGui::OpenPopup("##backup-auth");
        m_open_backup = false;
    }
    if (ImGui::BeginPopupModal(
            "##backup-auth", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        kit_title(tr("vault.backup"));
        kit_vspace(0.3f);
        ImGui::SetNextItemWidth(em * 12.0f);
        if (m_focus_backup) {
            ImGui::SetKeyboardFocusHere();
            m_focus_backup = false;
        }
        bool submit = secret_field(
            "##backup-pass", m_pass, secret_focus, tr("vault.passphrase"));
        kit_vspace(0.3f);
        if (kit_subtle_button(tr("ui.cancel"))) {
            sodium_memzero(m_pass.data(), m_pass.size());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        submit |= kit_primary_button(tr("vault.backup"));
        if (submit) {
            if (strnlen(m_pass.data(), m_pass.size()) == 0) {
                ev.err = "vault.msg.empty_pass";
            } else {
                ev.type = Event::Type::Backup;
                ev.pass = take_secret(m_pass);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    return ev;
}

}
