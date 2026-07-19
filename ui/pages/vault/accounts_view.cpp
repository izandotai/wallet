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
    bool& secret_focus, std::span<const std::string> addresses,
    std::span<const std::string> balances, uint32_t active, bool hd, bool watch)
{
    Event ev;
    const float em = ImGui::GetFontSize();

    kit_caption(tr("vault.address"));
    kit_vspace(0.15f);
    if (m_labels.size() < addresses.size())
        m_labels.resize(addresses.size());

    kit_group_begin("##accounts");
    // The note column is the elastic one: it shrinks before the
    // address does, and steps aside entirely when the pane is too
    // narrow to hold both.
    const float row_avail = ImGui::GetContentRegionAvail().x;
    const float spare = row_avail - em * (3.2f + 2.6f + 2.2f + 4.0f);
    const float note_w = spare > em * 6.0f ? em * 6.0f : spare;
    const bool show_note = note_w >= em * 2.5f;
    // The balance is garnish and the first thing to go when the pane
    // tightens; the address and its QR never leave.
    const bool show_balance = row_avail >= em * 15.0f;
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

        if (show_note) {
            ImGui::SameLine(em * 3.2f);
            ImGui::SetNextItemWidth(note_w);
            kit_text_field("##note", tr("wallet.note"), m_labels[i].data(),
                m_labels[i].size());
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                ev.type = Event::Type::LabelEdit;
                ev.index = i;
                ev.label = std::string(m_labels[i].data(),
                    strnlen(m_labels[i].data(), m_labels[i].size()));
            }
        }

        // Balance (when known), then the address right-aligned with
        // room reserved for the QR button, then the button itself.
        if (show_balance && i < balances.size()
            && !balances[std::size_t(i)].empty()) {
            ImGui::SameLine();
            ImGui::PushFont(nullptr, kit_caption_size());
            ImGui::TextDisabled("%s", balances[std::size_t(i)].c_str());
            ImGui::PopFont();
            // A balance is a snapshot; clicking it takes a fresh one.
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                kit_tooltip(tr("portfolio.refresh"));
            }
            if (ImGui::IsItemClicked()) {
                ev.type = Event::Type::RefreshBalance;
                ev.index = i;
            }
        }
        ImGui::SameLine();
        kit_copy_text_right("##addr", addresses[std::size_t(i)].c_str(),
            tr("ui.copy"), tr("ui.copied"), 2.6f);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()
            + ImGui::GetContentRegionAvail().x - em * 2.2f);
        // Explicit width: the row reserves exactly this slot, and the
        // auto-width floor would burst it.
        if (kit_subtle_button("QR", em * 2.2f)) {
            m_qr_index = int(i);
            m_open_qr = true;
        }
        ImGui::PopID();
    }
    if (hd) {
        kit_hairline();
        if (kit_link_button(tr("wallet.account.add")))
            ev.type = Event::Type::Add;
    }
    kit_group_end();

    if (!watch) {
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
    }

    // The receive QR: the address as a scannable code, plus the full
    // text to copy — a dialog because a code this small is useless.
    if (m_open_qr) {
        kit_dialog_open("##qr-view");
        m_open_qr = false;
    }
    if (kit_dialog_begin("##qr-view")) {
        if (m_qr_index >= 0 && std::size_t(m_qr_index) < addresses.size()) {
            const std::string& addr = addresses[std::size_t(m_qr_index)];
            const float qr_side = em * 9.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - qr_side) * 0.5f);
            kit_qr(addr.c_str(), 9.0f);
            kit_vspace(0.4f);
            kit_copy_text_centered(
                "##qr-addr", addr.c_str(), tr("ui.copy"), tr("ui.copied"));
            kit_vspace(0.3f);
            const float button_w = kit_button_width(tr("ui.back"));
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_w) * 0.5f);
            if (kit_subtle_button(tr("ui.back")))
                kit_dialog_close();
        }
        kit_dialog_end();
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
            kit_focus_here();
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
