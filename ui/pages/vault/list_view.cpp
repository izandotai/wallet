#include "ui/pages/vault/list_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/wallet/presets.hpp"
#include "ui/widgets/design.hpp"
#include "ui/widgets/dialog.hpp"
#include "ui/widgets/kit.hpp"

namespace izan::ui {

namespace {

    const char* kind_badge(const i18n::Catalog& tr, const std::string& kind)
    {
        const char* key = kind_badge_key(kind);
        return *key ? tr(key) : "";
    }

}

WalletListView::Event WalletListView::draw(const i18n::Catalog& tr, bool busy,
    const WalletStore& store, const std::string& active_id,
    bool active_unlocked)
{
    Event ev;
    ImGui::BeginDisabled(busy);

    const float em = ImGui::GetFontSize();
    kit_caption(tr("wallet.list"));
    kit_vspace(0.2f);

    for (const WalletEntry& w : store.wallets()) {
        ImGui::PushID(w.id.c_str());
        const bool is_active = w.id == active_id;

        // A source-list row, composed by hand: rounded selection
        // highlight, avatar, name over subtitle, a lock dot trailing.
        const float row_h = em * design().list_row_height;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float row_w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##row", ImVec2(row_w, row_h))
            && !is_active) {
            ev.type = Event::Type::Activate;
            ev.id = w.id;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (is_active || hovered)
            draw->AddRectFilled(pos, ImVec2(pos.x + row_w, pos.y + row_h),
                ImGui::GetColorU32(
                    is_active ? ImGuiCol_Header : ImGuiCol_HeaderHovered,
                    is_active ? 1.0f : 0.55f),
                em * design().selection_radius);

        const float avatar = em * design().list_avatar;
        kit_avatar_at(
            ImVec2(pos.x + em * 0.35f, pos.y + (row_h - avatar) * 0.5f),
            w.name.c_str(), avatar);

        const float text_x = pos.x + em * 0.35f + avatar + em * 0.45f;
        std::string subtitle = kind_badge(tr, w.kind);
        if (w.count > 1) {
            if (!subtitle.empty())
                subtitle += " · ";
            subtitle += std::to_string(w.count);
        }
        const float name_y = subtitle.empty() ? pos.y + (row_h - em) * 0.5f
                                              : pos.y + em * 0.28f;
        draw->AddText(ImVec2(text_x, name_y), ImGui::GetColorU32(ImGuiCol_Text),
            w.name.c_str());
        if (!subtitle.empty()) {
            ImGui::PushFont(nullptr, kit_caption_size());
            draw->AddText(ImGui::GetFont(), kit_caption_size(),
                ImVec2(text_x, name_y + em * 1.05f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), subtitle.c_str());
            ImGui::PopFont();
        }

        // Trailing lock state: filled accent dot when this wallet is
        // open, a quiet ring otherwise.
        const ImVec2 dot(pos.x + row_w - em * 0.7f, pos.y + row_h * 0.5f);
        if (is_active && active_unlocked)
            draw->AddCircleFilled(
                dot, em * 0.16f, ImGui::GetColorU32(kit_accent()));
        else
            draw->AddCircle(dot, em * 0.16f,
                ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f));

        if (ImGui::BeginPopupContextItem("##card-menu")) {
            // The click that opened this menu leaves a keyboard-nav
            // cursor behind; a mouse-born menu must not show a focus
            // ring on its first item.
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            if (ImGui::MenuItem(tr("wallet.activate")) && !is_active) {
                ev.type = Event::Type::Activate;
                ev.id = w.id;
            }
            if (ImGui::MenuItem(tr("wallet.rename"))) {
                m_target = w.id;
                sodium_memzero(m_rename.data(), m_rename.size());
                std::memcpy(m_rename.data(), w.name.data(),
                    std::min(w.name.size(), m_rename.size() - 1));
                m_open_rename = true;
            }
            if (ImGui::MenuItem(tr("wallet.delete"))) {
                m_target = w.id;
                sodium_memzero(m_confirm.data(), m_confirm.size());
                m_open_delete = true;
            }
            ImGui::PopItemFlag();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // New and Import live at the bottom of the pane, out of the way.
    const float footer = em * 2.2f;
    const float slack = ImGui::GetContentRegionAvail().y - footer;
    if (slack > 0.0f)
        ImGui::Dummy(ImVec2(0.0f, slack));
    if (kit_subtle_button(tr("vault.create")))
        ev.type = Event::Type::Create;
    ImGui::SameLine();
    if (kit_subtle_button(tr("vault.import")))
        ev.type = Event::Type::Import;

    // Dialogs are opened outside the card loop so their ids are stable.
    if (m_open_rename) {
        kit_dialog_open("##rename-wallet");
        m_open_rename = false;
    }
    if (m_open_delete) {
        kit_dialog_open("##delete-wallet");
        m_open_delete = false;
    }

    std::string target_name = m_target;
    for (const WalletEntry& w : store.wallets())
        if (w.id == m_target)
            target_name = w.name;

    if (kit_dialog_begin("##rename-wallet")) {
        kit_dialog_header_avatar(target_name.c_str(), tr("wallet.rename"));
        kit_dialog_field_width();
        const bool enter = ImGui::InputTextWithHint("##rename-name",
            tr("wallet.name"), m_rename.data(), m_rename.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        int choice = kit_dialog_buttons(tr("ui.cancel"), tr("wallet.rename"));
        if (enter)
            choice = 2;
        if (choice == 2) {
            ev.type = Event::Type::Rename;
            ev.id = m_target;
            ev.name = std::string(
                m_rename.data(), strnlen(m_rename.data(), m_rename.size()));
        }
        if (choice != 0)
            kit_dialog_close();
        kit_dialog_end();
    }

    bool dismissed = false;
    if (kit_dialog_begin("##delete-wallet", &dismissed)) {
        if (dismissed)
            sodium_memzero(m_confirm.data(), m_confirm.size());
        kit_dialog_header_avatar(
            target_name.c_str(), target_name.c_str(), tr("wallet.delete.warn"));
        kit_caption(tr("wallet.delete.confirm"));
        kit_dialog_field_width();
        ImGui::InputText("##confirm", m_confirm.data(), m_confirm.size());
        const std::string typed(
            m_confirm.data(), strnlen(m_confirm.data(), m_confirm.size()));
        const int choice = kit_dialog_buttons(
            tr("ui.cancel"), tr("wallet.delete"), typed == target_name, true);
        if (choice == 2) {
            ev.type = Event::Type::Delete;
            ev.id = m_target;
        }
        if (choice != 0) {
            sodium_memzero(m_confirm.data(), m_confirm.size());
            kit_dialog_close();
        }
        kit_dialog_end();
    }

    ImGui::EndDisabled();
    return ev;
}

}
