#include "ui/pages/vault/secret_view.hpp"

#include <imgui.h>

#include "ui/widgets/kit.hpp"

namespace izan::ui {

void SecretView::show(secure::SecureBytes secret, keyd::RevealKind kind)
{
    m_secret = std::move(secret);
    m_kind = kind;
    m_open_pending = true;
}

void SecretView::reset()
{
    m_secret.reset();
    m_open_pending = false;
}

void SecretView::draw_dialog(const i18n::Catalog& tr)
{
    if (m_open_pending) {
        kit_dialog_open("##show-secret");
        m_open_pending = false;
    }
    bool dismissed = false;
    if (!kit_dialog_begin("##show-secret", &dismissed))
        return;
    const bool seed = m_kind == keyd::RevealKind::SeedEntropy;
    kit_dialog_header_icon("🔑",
        seed ? tr("vault.mnemonic") : tr("vault.detect.key"),
        tr("vault.msg.created"));
    // Fixed-width wrapping only: wrapping to the auto-resizing window
    // itself feeds back into its width and shimmers horizontally.
    const float content = ImGui::GetFontSize() * design().dialog_width;
    if (!m_secret.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + content);
        ImGui::TextUnformatted(reinterpret_cast<const char*>(m_secret.data()));
        ImGui::PopTextWrapPos();
    }
    kit_vspace(0.3f);
    ImGui::PushFont(nullptr, kit_caption_size());
    ImGui::PushStyleColor(ImGuiCol_Text, kit_danger());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + content);
    ImGui::TextUnformatted(
        seed ? tr("vault.warn.backup") : tr("vault.warn.backup.key"));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    kit_vspace(0.3f);
    if (dismissed)
        reset();
    if (kit_subtle_button(tr("ui.back"))) {
        reset();
        kit_dialog_close();
    }
    kit_dialog_end();
}

}
