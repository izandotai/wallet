#include "ui/pages/vault/list_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/wallet/presets.hpp"
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

        std::string subtitle = kind_badge(tr, w.kind);
        if (w.count > 1) {
            if (!subtitle.empty())
                subtitle += " · ";
            subtitle += std::to_string(w.count);
        }
        if (kit_list_row("##row", w.name.c_str(), subtitle.c_str(), is_active,
                is_active && active_unlocked)
            && !is_active) {
            ev.type = Event::Type::Activate;
            ev.id = w.id;
        }

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

    // New and Import live at the bottom of the pane, stacked full
    // width — side by side they overflow a narrow sidebar — with the
    // accent fill marking the primary of the pair.
    const float footer
        = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    const float slack = ImGui::GetContentRegionAvail().y - footer;
    if (slack > 0.0f)
        ImGui::Dummy(ImVec2(0.0f, slack));
    const float bw = ImGui::GetContentRegionAvail().x;
    if (kit_primary_button(tr("vault.create"), bw))
        ev.type = Event::Type::Create;
    if (kit_subtle_button(tr("vault.import"), bw))
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
