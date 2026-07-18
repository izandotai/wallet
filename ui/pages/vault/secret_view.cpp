#include "ui/pages/vault/secret_view.hpp"

#include <imgui.h>

namespace izan::ui {

void SecretView::show(secure::SecureBytes secret, keyd::RevealKind kind)
{
    m_secret = std::move(secret);
    m_kind = kind;
}

void SecretView::reset()
{
    m_secret.reset();
}

bool SecretView::draw(const i18n::Catalog& tr)
{
    const bool seed = m_kind == keyd::RevealKind::SeedEntropy;
    ImGui::TextWrapped("%s", tr("vault.msg.created"));
    ImGui::Spacing();
    if (!m_secret.empty())
        ImGui::TextWrapped(
            "%s", reinterpret_cast<const char*>(m_secret.data()));
    ImGui::Spacing();
    ImGui::TextWrapped(
        "%s", seed ? tr("vault.warn.backup") : tr("vault.warn.backup.key"));
    ImGui::Spacing();
    if (ImGui::Button(tr("vault.lock"))) {
        reset();
        return true;
    }
    return false;
}

}
