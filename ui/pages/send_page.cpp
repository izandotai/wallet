#include "ui/pages/send_page.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include <imgui.h>

#include <sodium.h>

#include "core/crypto/eth.hpp"
#include "core/units/decimal.hpp"
#include "domain/chains/rpc_client.hpp"
#include "keyd/signer.hpp"
#include "ui/shell/ime.hpp"
#include "ui/widgets/kit.hpp"

namespace izan::ui {

namespace {

    using secure::SecureBytes;

    std::string load_file(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot read " + path.string());
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string hex_of(std::span<const uint8_t> bytes)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out = "0x";
        for (uint8_t b : bytes) {
            out += digits[b >> 4];
            out += digits[b & 0xf];
        }
        return out;
    }

    // "0x" + 40 hex chars → 20 bytes; the caller has already checksum-
    // validated, so this cannot fail quietly.
    std::array<uint8_t, 20> address_bytes(std::string_view checked)
    {
        auto nib = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9')
                return uint8_t(c - '0');
            if (c >= 'a' && c <= 'f')
                return uint8_t(c - 'a' + 10);
            return uint8_t(c - 'A' + 10);
        };
        std::array<uint8_t, 20> out {};
        for (int i = 0; i < 20; ++i)
            out[std::size_t(i)] = uint8_t(
                nib(checked[2 + 2 * i]) << 4 | nib(checked[3 + 2 * i]));
        return out;
    }

    void centered_text(const char* text, bool muted)
    {
        const float w = ImGui::CalcTextSize(text).x;
        const float slack = ImGui::GetContentRegionAvail().x - w;
        if (slack > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack * 0.5f);
        if (muted)
            ImGui::TextDisabled("%s", text);
        else
            ImGui::TextUnformatted(text);
    }

    void centered_caption(const char* text)
    {
        ImGui::PushFont(nullptr, kit_caption_size());
        centered_text(text, true);
        ImGui::PopFont();
    }

    // Machine-made messages (node errors) wear the footnote voice:
    // oblique fine print, generously inset from both edges.
    void error_note(const char* text, float left_x, float width)
    {
        const float inset = ImGui::GetFontSize() * 1.1f;
        ImGui::SetCursorPosX(left_x + inset);
        kit_footnote(text, width - inset * 2.0f);
    }

}

SendPage::SendPage(const std::filesystem::path& data_dir, VaultPage& vault)
    : m_registry(
          chains::ChainRegistry::from_json(load_file(data_dir / "chains.json")))
    , m_vault(vault)
{
}

SendPage::~SendPage()
{
    sodium_memzero(m_pass.data(), m_pass.size());
}

const chains::ChainSpec& SendPage::selected_chain() const
{
    return m_registry.all()[std::size_t(m_chain_index)];
}

void SendPage::reset_to_form()
{
    sodium_memzero(m_pass.data(), m_pass.size());
    m_tx = tx::Eip1559Tx {};
    m_proposal = 0;
    m_status.clear();
    m_stage = Stage::Form;
}

void SendPage::cancel_flow()
{
    if (m_proposal && m_vault.keyd())
        m_vault.keyd()->deny(m_proposal);
    m_job.reset();
    reset_to_form();
}

void SendPage::poll_job()
{
    if (!m_job)
        return;
    const int phase = m_job->phase.load();
    if (phase == 0)
        return;
    if (phase == 1) {
        if (m_stage == Stage::Quoting) {
            m_tx.nonce = m_job->nonce;
            m_tx.gas_limit = m_job->gas;
            m_tx.max_priority_fee_per_gas
                = m_job->fees.max_priority_fee_per_gas;
            m_tx.max_fee_per_gas = m_job->fees.max_fee_per_gas;
            m_stage = Stage::Review;
            m_focus_pass = true;
            m_job.reset();
            return;
        }
        m_stage = Stage::Done; // keep m_job: the screen reads its results
        return;
    }
    m_status = m_job->error;
    m_status_is_key = false;
    if (m_stage == Stage::Quoting) {
        m_stage = Stage::Form; // the dialog closes itself next frame
    } else if (m_stage == Stage::Delivering && m_job->step.load() == 0) {
        // Failed before anything was signed (wrong passphrase, keyd
        // refusal): nothing happened on-chain, the review screen takes
        // another try — the proposal is still pending in the queue.
        m_stage = Stage::Review;
        m_focus_pass = true;
    } else if (m_stage == Stage::Delivering) {
        m_stage = Stage::Failed; // keep m_job: hash may be displayable
        return;
    }
    m_job.reset();
}

void SendPage::draw(GLFWwindow* window, const i18n::Catalog& tr)
{
    poll_job();

    // A wallet switch means every draft, error and proposal here spoke
    // for somebody else; start the page over.
    if (m_vault.active() != m_wallet_seen) {
        m_wallet_seen = m_vault.active();
        m_job.reset();
        reset_to_form();
    }

    ImGui::Begin((std::string(tr("send.title")) + "###send-page").c_str());

    m_secret_focus = false;
    draw_form(tr);
    if (m_stage != Stage::Form)
        draw_confirm_dialog(tr);

    // Same red line as the vault page: no IME while the passphrase
    // field has focus.
    if (m_secret_focus != m_ime_disabled) {
        set_ime_enabled(window, !m_secret_focus);
        m_ime_disabled = m_secret_focus;
    }

    ImGui::End();
}

void SendPage::draw_form(const i18n::Catalog& tr)
{
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float col = std::min(avail, em * 22.0f);
    const float left = ImGui::GetCursorPosX() + (avail - col) * 0.5f;
    const chains::ChainSpec& chain = selected_chain();

    kit_vspace(0.8f);

    // The page's identity mark, echoed by the confirmation dialog.
    ImGui::PushFont(nullptr, kit_snap(em * 2.1f));
    centered_text("📤", false);
    ImGui::PopFont();
    kit_vspace(0.6f);

    // The narrative of a payment: who first, then how much, then send
    // — the recipient is the irreversible part and leads the form.
    ImGui::SetCursorPosX(left);
    ImGui::SetNextItemWidth(col);
    kit_address_field("##send-to", tr("send.to"), m_to.data(), m_to.size(),
        tr("ui.paste"), tr("ui.copy_action"), tr("ui.clear"),
        [](const char* s) { return !crypto::eth_checksum_address(s).empty(); });

    const bool to_present = m_to[0] != '\0';
    const bool to_valid
        = to_present && !crypto::eth_checksum_address(m_to.data()).empty();
    if (to_present && !to_valid)
        centered_caption(tr("send.err.address"));

    kit_vspace(0.35f);

    // The amount row carries the network as its badge: the coin being
    // counted IS the selected chain's native coin.
    ImGui::SetCursorPosX(left);
    ImGui::SetNextItemWidth(col);
    bool pick_chain = false;
    kit_amount_field("##send-amount", m_amount.data(), m_amount.size(),
        chain.name.c_str(), &pick_chain);
    if (pick_chain)
        ImGui::OpenPopup("##send-chain-pop");
    if (ImGui::BeginPopup("##send-chain-pop")) {
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        for (int i = 0; i < int(m_registry.all().size()); ++i) {
            const chains::ChainSpec& c = m_registry.all()[std::size_t(i)];
            if (ImGui::MenuItem(
                    c.name.c_str(), c.symbol.c_str(), i == m_chain_index))
                m_chain_index = i;
        }
        ImGui::PopItemFlag();
        ImGui::EndPopup();
    }

    kit_vspace(0.6f);

    const bool unlocked = m_vault.unlocked() && m_vault.keyd() != nullptr;
    // Only the EVM family has a transaction engine so far; a wallet
    // wearing a BTC or Solana preset receives on that chain but cannot
    // spend from here yet.
    const bool evm
        = keyd::preset_family(keyd::DerivePreset(m_vault.active_preset()))
        == keyd::ChainFamily::Eth;

    // The sender is not a choice — it is the active account, named.
    const std::string sender = m_vault.active_address();
    if (unlocked && !sender.empty()) {
        ImGui::PushFont(nullptr, kit_caption_size());
        const std::string who = m_vault.active_name() + " · "
            + kit_elide_middle(sender.c_str(), em * 6.5f, kit_caption_size());
        const float av = em * 1.05f;
        const float total
            = av + em * 0.35f + ImGui::CalcTextSize(who.c_str()).x;
        ImGui::SetCursorPosX(left + (col - total) * 0.5f);
        kit_avatar(m_vault.active_name().c_str(), av);
        ImGui::SameLine(0.0f, em * 0.35f);
        ImGui::TextDisabled("%s", who.c_str());
        ImGui::PopFont();
    } else if (!unlocked) {
        centered_caption(tr("send.state.locked"));
    }
    if (unlocked && !evm)
        centered_caption(tr("send.err.family"));

    kit_vspace(0.5f);

    const bool ready = unlocked && evm && to_valid && m_amount[0] != '\0';
    ImGui::SetCursorPosX(left);
    ImGui::BeginDisabled(!ready || m_job != nullptr);
    if (kit_primary_button(tr("send.review_send"), col))
        begin_review();
    ImGui::EndDisabled();

    if (m_stage == Stage::Form && !m_status.empty()) {
        kit_vspace(0.25f);
        error_note(m_status_is_key ? tr(m_status.c_str()) : m_status.c_str(),
            left, col);
    }
}

void SendPage::begin_review()
{
    m_status.clear();
    m_to_checked = crypto::eth_checksum_address(m_to.data());
    try {
        m_tx = tx::Eip1559Tx {};
        m_tx.chain_id = selected_chain().chain_id;
        m_tx.to = address_bytes(m_to_checked);
        m_tx.value
            = units::parse_units(m_amount.data(), selected_chain().decimals);
    } catch (const std::exception&) {
        m_status = "send.err.amount";
        m_status_is_key = true;
        return;
    }

    // The sender address is a quick keyd round trip; fetch it on this
    // thread so the job never touches keyd while form buttons are
    // alive. Account index and derivation preset are captured here —
    // the quote, the envelope and the signature all speak for the same
    // identity even if the selection changes later.
    m_account = m_vault.active_account();
    m_preset = m_vault.active_preset();
    auto from = m_vault.keyd()->address(m_account, m_preset);
    if (!from) {
        m_status = m_vault.keyd()->last_error();
        m_status_is_key = false;
        return;
    }
    m_from = *from;
    auto job = std::make_shared<Job>();
    m_job = job;
    m_stage = Stage::Quoting;
    kit_dialog_open("##send-confirm");
    std::thread([job, spec = selected_chain(), from = m_from,
                    draft = m_tx]() mutable {
        try {
            chains::RpcClient rpc(std::move(spec));
            job->nonce = tx::next_nonce(rpc, from);
            job->gas = tx::estimate_gas(rpc, from, draft);
            job->fees = tx::quote_fees(rpc);
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        }
    }).detach();
}

void SendPage::confirm_send()
{
    if (strnlen(m_pass.data(), m_pass.size()) == 0)
        return;
    if (!m_vault.keyd()) {
        m_status = "keyd gone";
        m_status_is_key = false;
        return;
    }
    if (m_proposal == 0) {
        // Envelope v2: preset and account index travel inside the
        // queued bytes, so the identity the human approves — down to
        // its derivation dialect — is the identity that signs.
        auto id = m_vault.keyd()->submit_ui(
            keyd::make_envelope(keyd::DerivePreset(m_preset), m_account,
                tx::signing_payload(m_tx)));
        if (!id) {
            m_status = m_vault.keyd()->last_error();
            m_status_is_key = false;
            return;
        }
        m_proposal = *id;
    }

    SecureBytes pass = take_secret(m_pass);
    auto job = std::make_shared<Job>();
    m_job = job;
    m_stage = Stage::Delivering;
    m_status.clear();
    // The job borrows keyd for the one approve round trip; the dialog
    // shows no buttons while it runs, so this page keeps the
    // single-driver rule.
    std::thread([job, keyd = m_vault.keyd(), spec = selected_chain(),
                    draft = m_tx, proposal = m_proposal,
                    pass = std::move(pass)]() mutable {
        try {
            auto sig = keyd->approve(proposal, pass);
            pass.reset();
            if (!sig)
                throw std::runtime_error(keyd->last_error());
            const std::vector<uint8_t> raw
                = tx::encode_signed(draft, sig->y_parity, sig->r, sig->s);
            job->step.store(1);
            chains::RpcClient rpc(std::move(spec));
            const std::array<uint8_t, 32> hash = tx::broadcast(rpc, raw);
            job->tx_hash = hex_of(hash);
            job->step.store(2);
            for (int tries = 0; tries < 150; ++tries) {
                if (auto receipt = tx::transaction_receipt(rpc, hash)) {
                    job->tx_success = receipt->success;
                    job->block = receipt->block_number;
                    job->phase.store(1);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::seconds(4));
            }
            throw std::runtime_error("no receipt after 10 minutes; "
                                     "check the explorer before retrying");
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        }
    }).detach();
}

void SendPage::draw_confirm_dialog(const i18n::Catalog& tr)
{
    const float em = ImGui::GetFontSize();
    const float content = em * design().dialog_width;
    bool dismissed = false;
    // Money in flight has no cancel key: once the approve job starts,
    // the dialog stays until the chain answers.
    const bool escapable = m_stage != Stage::Delivering;
    if (!kit_dialog_begin("##send-confirm", &dismissed, escapable))
        return;
    if (dismissed) {
        cancel_flow();
        kit_dialog_end();
        return;
    }
    if (m_stage == Stage::Form) {
        kit_dialog_close();
        kit_dialog_end();
        return;
    }

    const chains::ChainSpec& chain = selected_chain();
    const std::string amount
        = units::format_units(m_tx.value, chain.decimals) + " " + chain.symbol;

    // An elided value must stay reviewable: hovering any shortened row
    // reveals the whole thing.
    auto row = [&](const char* label, const std::string& value) {
        const float x0 = ImGui::GetCursorPosX();
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        const float used = ImGui::GetCursorPosX() - x0;
        const std::string shown
            = kit_elide_middle(value.c_str(), content - used - em * 0.6f);
        ImGui::SetCursorPosX(
            x0 + content - ImGui::CalcTextSize(shown.c_str()).x);
        ImGui::TextUnformatted(shown.c_str());
        if (shown != value && ImGui::IsItemHovered())
            kit_tooltip(value.c_str());
    };

    auto hero = [&](const char* text) {
        ImGui::PushFont(nullptr, kit_snap(em * 1.7f));
        centered_text(text, false);
        ImGui::PopFont();
    };

    switch (m_stage) {
    case Stage::Quoting: {
        kit_vspace(0.5f);
        const float r = em * 0.7f;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + (content - r * 2.0f) * 0.5f);
        kit_spinner(0.7f);
        kit_vspace(0.25f);
        centered_caption(tr("send.quoting"));
        kit_vspace(0.5f);
        if (kit_subtle_button(tr("ui.cancel"), content)) {
            cancel_flow();
            kit_dialog_close();
        }
        break;
    }
    case Stage::Review: {
        kit_dialog_header_icon("📤", amount.c_str(), chain.name.c_str());

        const units::U256 fee_max
            = m_tx.max_fee_per_gas.checked_mul_u64(m_tx.gas_limit);
        const std::string fee
            = units::format_units(fee_max, chain.decimals) + " " + chain.symbol;
        const std::string total
            = units::format_units(
                  m_tx.value.checked_add(fee_max), chain.decimals)
            + " " + chain.symbol;

        row(tr("send.from"), m_vault.active_name() + " · " + m_from);
        row(tr("send.to"), m_to_checked);
        row(tr("send.fee_max"), "≤ " + fee);
        row(tr("send.total_max"), "≤ " + total);

        char plumbing[128];
        std::snprintf(plumbing, sizeof plumbing,
            "nonce %llu · gas %llu · %s gwei", (unsigned long long)m_tx.nonce,
            (unsigned long long)m_tx.gas_limit,
            units::format_units(m_tx.max_fee_per_gas, 9).c_str());
        centered_caption(plumbing);
        kit_vspace(0.4f);

        if (m_focus_pass) {
            kit_focus_here();
            m_focus_pass = false;
        }
        kit_dialog_field_width();
        const bool submitted = secret_field(
            "##send-pass", m_pass, m_secret_focus, tr("send.passphrase"));
        if (!m_status.empty()) {
            error_note(
                m_status_is_key ? tr(m_status.c_str()) : m_status.c_str(),
                ImGui::GetCursorPosX(), content);
        }
        const bool has_pass = strnlen(m_pass.data(), m_pass.size()) > 0;
        const int choice
            = kit_dialog_buttons(tr("ui.cancel"), tr("send.confirm"), has_pass);
        if (choice == 1) {
            cancel_flow();
            kit_dialog_close();
        } else if (choice == 2 || (submitted && has_pass)) {
            confirm_send();
        }
        break;
    }
    case Stage::Delivering: {
        kit_vspace(0.5f);
        const int step = m_job ? m_job->step.load() : 0;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + (content - em * 1.4f) * 0.5f);
        kit_spinner(0.7f);
        kit_vspace(0.25f);
        centered_caption(step == 0 ? tr("send.signing")
                : step == 1        ? tr("send.broadcasting")
                                   : tr("send.waiting"));
        if (step >= 2 && m_job) {
            kit_vspace(0.25f);
            kit_copy_text_centered("##send-hash", m_job->tx_hash.c_str(),
                tr("send.hash"), tr("ui.copied"));
        }
        break;
    }
    case Stage::Done:
    case Stage::Failed: {
        const bool ok = m_stage == Stage::Done && m_job && m_job->tx_success;
        kit_vspace(0.25f);
        kit_result_mark(ok);
        kit_vspace(0.25f);
        if (m_stage == Stage::Done && m_job) {
            hero(amount.c_str());
            centered_caption(ok ? tr("send.confirmed") : tr("send.reverted"));
            char block[64];
            std::snprintf(block, sizeof block, "%s %llu", tr("send.block"),
                (unsigned long long)m_job->block);
            centered_caption(block);
        } else {
            centered_caption(tr("send.failed"));
            if (!m_status.empty())
                error_note(
                    m_status_is_key ? tr(m_status.c_str()) : m_status.c_str(),
                    ImGui::GetCursorPosX(), content);
        }
        if (m_job && !m_job->tx_hash.empty()) {
            kit_vspace(0.25f);
            kit_copy_text_centered("##send-hash", m_job->tx_hash.c_str(),
                tr("send.hash"), tr("ui.copied"));
            if (!chain.explorer.empty()) {
                const std::string link
                    = chain.explorer + "/tx/" + m_job->tx_hash;
                const float w = ImGui::CalcTextSize(link.c_str()).x;
                if (w < content)
                    ImGui::SetCursorPosX(
                        ImGui::GetCursorPosX() + (content - w) * 0.5f);
                kit_hyperlink("##send-link", link.c_str(), link.c_str());
            }
        }
        kit_vspace(0.4f);
        if (kit_primary_button(tr("ui.done"), content)) {
            // A delivered send leaves a blank form behind it; a failed
            // one keeps the figures for another go.
            if (m_stage == Stage::Done) {
                std::memset(m_to.data(), 0, m_to.size());
                std::memset(m_amount.data(), 0, m_amount.size());
            }
            m_job.reset();
            reset_to_form();
            kit_dialog_close();
        }
        break;
    }
    default:
        break;
    }

    kit_dialog_end();
}

}
