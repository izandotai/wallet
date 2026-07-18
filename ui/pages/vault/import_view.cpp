#include "ui/pages/vault/import_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/wallet/presets.hpp"
#include "ui/widgets/secret_field.hpp"

namespace izan::ui {

void ImportView::reset()
{
    m_model.reset();
    sodium_memzero(m_name.data(), m_name.size());
    sodium_memzero(m_secret_in.data(), m_secret_in.size());
    sodium_memzero(m_pass.data(), m_pass.size());
    sodium_memzero(m_confirm.data(), m_confirm.size());
}

ImportView::Event ImportView::draw(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, const WalletStore& store)
{
    Event ev;
    ImGui::InputText(tr("wallet.name"), m_name.data(), m_name.size());
    ImGui::TextUnformatted(tr("vault.secret_in"));
    if (ImGui::InputTextMultiline("##secret-in", m_secret_in.data(),
            m_secret_in.size(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4)))
        m_model.update(std::string_view(m_secret_in.data(),
            strnlen(m_secret_in.data(), m_secret_in.size())));
    secret_focus |= ImGui::IsItemActive();

    // The recognition line: what the pasted text is, updated as it
    // changes — nobody should have to press Import to find out.
    ImGui::TextDisabled("%s", tr("vault.detect"));
    ImGui::SameLine();
    switch (m_model.kind()) {
    case crypto::SecretKind::Mnemonic:
        ImGui::TextUnformatted(tr("vault.detect.mnemonic"));
        break;
    case crypto::SecretKind::RawKey:
        ImGui::TextUnformatted(tr("vault.detect.key"));
        break;
    case crypto::SecretKind::Wif:
        ImGui::TextUnformatted(tr("vault.detect.wif"));
        break;
    case crypto::SecretKind::Unrecognized:
        ImGui::TextUnformatted(tr("vault.detect.none"));
        break;
    }

    // At index 0 MetaMask and Ledger Live share a path, so two rows may
    // show the same address — still two different wallets from account
    // 1 on.
    if (!m_model.offered().empty()) {
        ImGui::TextDisabled("%s", tr("wallet.preset"));
        for (const keyd::DerivePreset p : m_model.offered()) {
            ImGui::PushID(int(p));
            if (ImGui::RadioButton(
                    "##preset", m_model.selected() == uint8_t(p)))
                m_model.select(p);
            ImGui::SameLine();
            ImGui::TextUnformatted(preset_name(p));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_model.preview(p).c_str());
            ImGui::PopID();
        }
    }

    secret_field(tr("vault.passphrase"), m_pass, secret_focus);
    secret_field(tr("vault.passphrase.confirm"), m_confirm, secret_focus);

    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("vault.import"))) {
        const std::string name(
            m_name.data(), strnlen(m_name.data(), m_name.size()));
        const std::string_view text(m_secret_in.data(),
            strnlen(m_secret_in.data(), m_secret_in.size()));
        if (!store.valid_new_name(name)) {
            ev.err = "wallet.err.name";
        } else if (strnlen(m_pass.data(), m_pass.size()) == 0) {
            ev.err = "vault.msg.empty_pass";
        } else if (std::strncmp(m_pass.data(), m_confirm.data(), m_pass.size())
            != 0) {
            ev.err = "vault.msg.mismatch";
        } else if (auto wallet = m_model.build(text)) {
            ev.type = Event::Type::Submit;
            ev.name = name;
            ev.pass = take_secret(m_pass);
            ev.wallet = std::move(wallet);
            ev.preset = m_model.selected();
            sodium_memzero(m_confirm.data(), m_confirm.size());
            sodium_memzero(m_secret_in.data(), m_secret_in.size());
        } else {
            // Content decides what this is; anything unrecognized is
            // refused, never guessed at.
            ev.err = "vault.err.secret";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("<##import-back")) {
        reset();
        ev.type = Event::Type::Back;
    }
    if (busy)
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
    return ev;
}

}
