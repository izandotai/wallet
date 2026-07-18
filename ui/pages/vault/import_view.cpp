#include "ui/pages/vault/import_view.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/wallet/presets.hpp"
#include "ui/widgets/kit.hpp"
#include "ui/widgets/secret_field.hpp"

namespace izan::ui {

namespace {

    // Two dots, the lit one saying where you are — no words needed.
    void step_dots(int step)
    {
        const float em = ImGui::GetFontSize();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (int i = 0; i < 2; ++i) {
            const ImVec2 c(
                pos.x + em * 0.25f + float(i) * em * 0.75f, pos.y + em * 0.5f);
            if (i == step)
                draw->AddCircleFilled(
                    c, em * 0.16f, ImGui::GetColorU32(kit_accent()));
            else
                draw->AddCircle(c, em * 0.16f,
                    ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f));
        }
        ImGui::Dummy(ImVec2(em * 1.5f, em));
    }

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
    step_dots(m_step == Step::Paste ? 0 : 1);
    kit_vspace(0.4f);

    if (m_step == Step::Paste) {
        kit_group_begin("##paste");
        if (m_focus_pending && !busy) {
            ImGui::SetKeyboardFocusHere();
            m_focus_pending = false;
        }
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        if (ImGui::InputTextMultiline("##secret-in", m_secret_in.data(),
                m_secret_in.size(),
                ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4)))
            m_model.update(std::string_view(m_secret_in.data(),
                strnlen(m_secret_in.data(), m_secret_in.size())));
        ImGui::PopStyleColor();
        secret_focus |= ImGui::IsItemActive();
        kit_group_end();
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

                const bool chosen = m_model.selected() == uint8_t(p);
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float row_h = em * 1.4f;
                if (ImGui::InvisibleButton("##pick",
                        ImVec2(ImGui::GetContentRegionAvail().x, row_h)))
                    m_model.select(p);
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 mark(pos.x + em * 0.4f, pos.y + row_h * 0.5f);
                if (chosen)
                    draw->AddCircleFilled(
                        mark, em * 0.28f, ImGui::GetColorU32(kit_accent()));
                else
                    draw->AddCircle(mark, em * 0.28f,
                        ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f));
                draw->AddText(
                    ImVec2(pos.x + em * 1.1f, pos.y + (row_h - em) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), preset_name(p));
                ImGui::PushFont(nullptr, kit_caption_size());
                const float addr_y
                    = pos.y + (row_h - kit_caption_size()) * 0.5f;
                draw->AddText(ImGui::GetFont(), kit_caption_size(),
                    ImVec2(pos.x + em * 11.0f, addr_y),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    m_model.preview(p).c_str());
                ImGui::PopFont();
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

    const float col = em * 14.0f;
    kit_group_begin("##import-fields", col + em * 1.2f);
    ImGui::SetNextItemWidth(col);
    if (m_focus_pending && !busy) {
        ImGui::SetKeyboardFocusHere();
        m_focus_pending = false;
    }
    ImGui::InputTextWithHint(
        "##name", tr("wallet.name"), m_name.data(), m_name.size());
    kit_hairline();
    ImGui::SetNextItemWidth(col);
    secret_field("##pass", m_pass, secret_focus, tr("vault.passphrase"));
    ImGui::SetNextItemWidth(col);
    bool submit = secret_field(
        "##confirm", m_confirm, secret_focus, tr("vault.passphrase.confirm"));
    kit_group_end();
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
