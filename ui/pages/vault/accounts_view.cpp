#include "ui/pages/vault/accounts_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/kit.hpp"

namespace izan::ui {

void AccountsView::reset()
{
    sodium_memzero(m_pass.data(), m_pass.size());
    m_labels.clear();
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

        // Fixed columns, one baseline: mark, index, note, then the
        // address flush right — every row lines up with every other.
        ImGui::AlignTextToFramePadding();
        if (kit_selection_mark("##select", i == active) && i != active) {
            ev.type = Event::Type::Select;
            ev.index = i;
        }

        ImGui::SameLine(em * 1.6f);
        ImGui::PushFont(nullptr, kit_caption_size());
        ImGui::Text("#%u", i);
        ImGui::PopFont();

        ImGui::SameLine(em * 3.2f);
        ImGui::SetNextItemWidth(em * 6.0f);
        kit_text_field("##note", tr("wallet.note"), m_labels[i].data(),
            m_labels[i].size());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ev.type = Event::Type::LabelEdit;
            ev.index = i;
            ev.label = std::string(m_labels[i].data(),
                strnlen(m_labels[i].data(), m_labels[i].size()));
        }

        // Address: right-aligned to the row's edge, middle-elided to
        // whatever space the row leaves, click to copy — the
        // confirmation takes the address's place.
        ImGui::SameLine();
        kit_copy_text_right("##addr", addresses[std::size_t(i)].c_str(),
            tr("ui.copy"), tr("ui.copied"));
        ImGui::PopID();
    }
    if (hd) {
        kit_hairline();
        if (kit_link_button(tr("wallet.account.add")))
            ev.type = Event::Type::Add;
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
        kit_dialog_open("##backup-auth");
        m_open_backup = false;
    }
    bool dismissed = false;
    if (kit_dialog_begin("##backup-auth", &dismissed)) {
        if (dismissed)
            sodium_memzero(m_pass.data(), m_pass.size());
        kit_dialog_header_icon(
            "🔒", tr("vault.backup"), tr("vault.warn.backup"));
        kit_dialog_field_width();
        if (m_focus_backup) {
            ImGui::SetKeyboardFocusHere();
            m_focus_backup = false;
        }
        const bool enter = secret_field(
            "##backup-pass", m_pass, secret_focus, tr("vault.passphrase"));
        int choice = kit_dialog_buttons(tr("ui.cancel"), tr("vault.backup"));
        if (enter)
            choice = 2;
        if (choice == 2) {
            if (strnlen(m_pass.data(), m_pass.size()) == 0) {
                ev.err = "vault.msg.empty_pass";
            } else {
                ev.type = Event::Type::Backup;
                ev.pass = take_secret(m_pass);
                kit_dialog_close();
            }
        } else if (choice == 1) {
            sodium_memzero(m_pass.data(), m_pass.size());
            kit_dialog_close();
        }
        kit_dialog_end();
    }
    return ev;
}

}
