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

    // ASCII-fold substring match; multibyte names compare raw, which
    // is exactly what typing a CJK fragment expects.
    bool name_matches(const std::string& name, const char* needle)
    {
        if (!*needle)
            return true;
        auto fold = [](std::string s) {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
            return s;
        };
        return fold(name).find(fold(needle)) != std::string::npos;
    }

}

WalletListView::Event WalletListView::draw(const i18n::Catalog& tr, bool busy,
    const WalletStore& store, const std::string& active_id,
    bool active_unlocked)
{
    Event ev;
    ImGui::BeginDisabled(busy);

    // The pinned header: the two doors full width, the filter under
    // them — a list of a thousand wallets buries neither.
    const float em = ImGui::GetFontSize();
    const float bw = ImGui::GetContentRegionAvail().x;
    if (kit_primary_button(tr("vault.create"), bw))
        ev.type = Event::Type::Create;
    if (kit_subtle_button(tr("vault.import"), bw))
        ev.type = Event::Type::Import;
    kit_vspace(0.15f);
    ImGui::SetNextItemWidth(bw);
    kit_search_field(
        "##filter", tr("wallet.filter"), m_filter.data(), m_filter.size());
    kit_vspace(0.35f);

    // The cards scroll under the header in their own region; this
    // child really scrolls, so it earns the wheel it captures. Padded
    // evenly on all four sides — rows must not kiss the region's rim.
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding, ImVec2(em * 0.45f, em * 0.45f));
    ImGui::BeginChild("##wallet-scroll", ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_AlwaysUseWindowPadding);
    const char* filter = m_filter.data();
    for (const WalletEntry& w : store.wallets()) {
        if (!name_matches(w.name, filter))
            continue;
        ImGui::PushID(w.id.c_str());
        const bool is_active = w.id == active_id;

        std::string subtitle = kind_badge(tr, w.kind);
        if (w.count > 1) {
            if (!subtitle.empty())
                subtitle += " · ";
            subtitle += std::to_string(w.count);
        }
        if (w.pinned)
            subtitle = subtitle.empty() ? "📌" : "📌 " + subtitle;
        if (kit_list_row("##row", w.name.c_str(), subtitle.c_str(), is_active,
                is_active && active_unlocked)
            && !is_active) {
            ev.type = Event::Type::Activate;
            ev.id = w.id;
        }

        ImGui::OpenPopupOnItemClick(
            "##card-menu", ImGuiPopupFlags_MouseButtonRight);
        if (kit_menu_begin("##card-menu")) {
            if (kit_menu_item(tr("wallet.activate")) && !is_active) {
                ev.type = Event::Type::Activate;
                ev.id = w.id;
            }
            if (kit_menu_item(tr(w.pinned ? "wallet.unpin" : "wallet.pin"))) {
                ev.type = Event::Type::Pin;
                ev.id = w.id;
            }
            if (kit_menu_item(tr("wallet.rename"))) {
                m_target = w.id;
                sodium_memzero(m_rename.data(), m_rename.size());
                std::memcpy(m_rename.data(), w.name.data(),
                    std::min(w.name.size(), m_rename.size() - 1));
                m_open_rename = true;
            }
            if (kit_menu_item(tr("wallet.delete"))) {
                m_target = w.id;
                sodium_memzero(m_confirm.data(), m_confirm.size());
                m_open_delete = true;
            }
            kit_menu_end();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

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
        // 开窗即入井：对话框存在的唯一目的就是打字
        if (ImGui::IsWindowAppearing())
            kit_focus_here();
        const bool enter = kit_text_field("##rename-name", tr("wallet.name"),
            m_rename.data(), m_rename.size());
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
        if (ImGui::IsWindowAppearing())
            kit_focus_here();
        // 回车即确认——但只在名字对上时；名字不对回车与灰钮同哑
        const bool enter = kit_text_field("##confirm", tr("wallet.name"),
            m_confirm.data(), m_confirm.size());
        const std::string typed(
            m_confirm.data(), strnlen(m_confirm.data(), m_confirm.size()));
        int choice = kit_dialog_buttons(
            tr("ui.cancel"), tr("wallet.delete"), typed == target_name, true);
        if (enter && typed == target_name)
            choice = 2;
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
