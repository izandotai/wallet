#include "ui/pages/vault/import_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/wallet/presets.hpp"
#include "ui/widgets/kit.hpp"

namespace izan::ui {

namespace {

    const char* detect_text(const i18n::Catalog& tr, crypto::SecretKind kind)
    {
        switch (kind) {
        case crypto::SecretKind::Mnemonic:
            return tr("vault.detect.mnemonic");
        case crypto::SecretKind::RawKey:
            return tr("vault.detect.key");
        case crypto::SecretKind::Wif:
            return tr("vault.detect.wif");
        case crypto::SecretKind::SolKey:
            return tr("vault.detect.solkey");
        case crypto::SecretKind::Unrecognized:
            break;
        }
        return tr("vault.detect.none");
    }

}

void ImportView::reset()
{
    m_model.reset();
    m_step = Step::Paste;
    m_focus_pending = true;
    sodium_memzero(m_name.data(), m_name.size());
    sodium_memzero(m_secret_in.data(), m_secret_in.size());
    sodium_memzero(m_pass.data(), m_pass.size());
    sodium_memzero(m_confirm.data(), m_confirm.size());
}

ImportView::Event ImportView::draw(const i18n::Catalog& tr, bool busy,
    bool& secret_focus, const WalletStore& store)
{
    Event ev;
    const float em = ImGui::GetFontSize();

    kit_title(tr("vault.import"));
    ImGui::SameLine();
    // The dots ride the title's line; nudge them onto its center.
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + (kit_title_size() - em) * 0.5f);
    kit_step_dots(m_step == Step::Paste ? 0 : 1, 2);
    kit_vspace(0.4f);

    if (m_step == Step::Paste) {
        if (m_focus_pending && !busy) {
            kit_focus_here();
            m_focus_pending = false;
        }
        if (kit_paste_box("##secret-in", tr("vault.secret_in"),
                m_secret_in.data(), m_secret_in.size(), 4.0f, secret_focus))
            m_model.update(std::string_view(m_secret_in.data(),
                strnlen(m_secret_in.data(), m_secret_in.size())));
        kit_vspace(0.2f);

        // The recognition line: what the pasted text is, updated as it
        // changes — nobody should have to press Import to find out.
        kit_pill(detect_text(tr, m_model.kind()),
            m_model.recognized()
                ? kit_accent()
                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        // At index 0 MetaMask and Ledger Live share a path, so two rows
        // may show the same address — still two different wallets from
        // account 1 on.
        if (!m_model.offered().empty()) {
            kit_vspace(0.3f);
            kit_group_begin("##presets");
            bool first = true;
            for (const keyd::DerivePreset p : m_model.offered()) {
                ImGui::PushID(int(p));
                if (!first)
                    kit_hairline();
                first = false;
                if (kit_choice_row("##pick", preset_name(p),
                        m_model.preview(p).c_str(),
                        m_model.selected() == uint8_t(p)))
                    m_model.select(p);
                ImGui::PopID();
            }
            kit_group_end();
        }
        kit_vspace(0.4f);

        if (kit_subtle_button(tr("ui.back"))) {
            reset();
            ev.type = Event::Type::Back;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_model.recognized());
        if (kit_primary_button(tr("ui.continue"))) {
            m_step = Step::Confirm;
            m_focus_pending = true;
        }
        ImGui::EndDisabled();
        return ev;
    }

    // Step::Confirm — the person has seen the address; now the wallet
    // gets its name and its passphrase.
    const keyd::DerivePreset chosen = keyd::DerivePreset(m_model.selected());
    ImGui::TextUnformatted(preset_name(chosen));
    kit_caption(m_model.preview(chosen).c_str());
    kit_vspace(0.4f);

    const float form_avail = ImGui::GetContentRegionAvail().x;
    const float col = em * design().form_width < form_avail
        ? em * design().form_width
        : form_avail;
    ImGui::SetNextItemWidth(col);
    if (m_focus_pending && !busy) {
        kit_focus_here();
        m_focus_pending = false;
    }
    kit_text_field("##name", tr("wallet.name"), m_name.data(), m_name.size());
    ImGui::SetNextItemWidth(col);
    secret_field("##pass", m_pass, secret_focus, tr("vault.passphrase"));
    ImGui::SetNextItemWidth(col);
    bool submit = secret_field(
        "##confirm", m_confirm, secret_focus, tr("vault.passphrase.confirm"));
    kit_vspace(0.5f);

    ImGui::BeginDisabled(busy);
    if (kit_subtle_button(tr("ui.back")))
        m_step = Step::Paste;
    ImGui::SameLine();
    submit |= kit_primary_button(tr("vault.import"));
    if (submit) {
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
    if (busy) {
        kit_vspace(0.25f);
        ImGui::TextDisabled("%s", tr("vault.busy.creating"));
    }
    return ev;
}

}
