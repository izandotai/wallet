#include "ui/pages/swap_page.hpp"

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
#include "domain/assets/balances.hpp"
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

    void error_note(const char* text, float left_x, float width)
    {
        const float inset = ImGui::GetFontSize() * 1.1f;
        ImGui::SetCursorPosX(left_x + inset);
        kit_footnote(text, width - inset * 2.0f);
    }

}

SwapPage::SwapPage(const std::filesystem::path& data_dir,
    const std::filesystem::path& user_dir, VaultPage& vault)
    : m_registry(
          chains::ChainRegistry::from_json(load_file(data_dir / "chains.json")))
    , m_vault(vault)
{
    assets::TokenRegistry tokens
        = assets::TokenRegistry::from_json(load_file(data_dir / "tokens.json"));
    try {
        tokens.extend(assets::TokenRegistry::from_json(
                          load_file(user_dir / "tokens.user.json")),
            m_registry);
    } catch (const std::exception&) {
        // absent or malformed: the shipped set stands alone
    }
    for (int i = 0; i < int(m_registry.all().size()); ++i) {
        const chains::ChainSpec& chain = m_registry.all()[std::size_t(i)];
        m_assets.push_back({ i, chain.symbol, "", chain.decimals });
        for (const assets::TokenSpec* token : tokens.tokens_for(chain.chain_id))
            m_assets.push_back(
                { i, token->symbol, token->address, token->decimals });
    }
    m_buy_index = first_partner(m_sell_index);
}

SwapPage::~SwapPage()
{
    sodium_memzero(m_pass.data(), m_pass.size());
}

const chains::ChainSpec& SwapPage::selected_chain() const
{
    return m_registry.all()[std::size_t(sell().chain)];
}

void SwapPage::prefill(uint64_t chain_id, const std::string& symbol)
{
    for (int i = 0; i < int(m_assets.size()); ++i) {
        const Asset& a = m_assets[std::size_t(i)];
        if (a.symbol == symbol
            && m_registry.all()[std::size_t(a.chain)].chain_id == chain_id) {
            m_sell_index = i;
            if (buy().chain != a.chain || m_buy_index == i)
                m_buy_index = first_partner(i);
            break;
        }
    }
    m_focus_self = true;
}

int SwapPage::first_partner(int sell_index) const
{
    const int chain = m_assets[std::size_t(sell_index)].chain;
    for (int i = 0; i < int(m_assets.size()); ++i)
        if (i != sell_index && m_assets[std::size_t(i)].chain == chain)
            return i;
    return sell_index; // a chain with one asset swaps nothing
}

void SwapPage::reset_to_form()
{
    sodium_memzero(m_pass.data(), m_pass.size());
    m_tx_approve = tx::Eip1559Tx {};
    m_tx_swap = tx::Eip1559Tx {};
    m_need_approve = false;
    m_proposal_approve = 0;
    m_proposal_swap = 0;
    m_status.clear();
    m_stage = Stage::Form;
}

void SwapPage::cancel_flow()
{
    if (m_vault.keyd()) {
        if (m_proposal_approve)
            m_vault.keyd()->deny(m_proposal_approve);
        if (m_proposal_swap)
            m_vault.keyd()->deny(m_proposal_swap);
    }
    m_job.reset();
    reset_to_form();
}

void SwapPage::poll_job()
{
    if (!m_job)
        return;
    const int phase = m_job->phase.load();
    if (phase == 0)
        return;
    if (phase == 1) {
        if (m_stage == Stage::Quoting) {
            m_quote = m_job->quote;
            m_need_approve = m_job->need_approve;
            // The approval, when needed: exact amount, first in line.
            const chains::ChainSpec& chain = selected_chain();
            if (m_need_approve) {
                m_tx_approve = tx::Eip1559Tx {};
                m_tx_approve.chain_id = chain.chain_id;
                m_tx_approve.to = address_bytes(sell().token);
                m_tx_approve.data = assets::erc20_approve_calldata(
                    m_quote.approval_address, m_quote.from_amount);
                m_tx_approve.nonce = m_job->nonce;
                m_tx_approve.gas_limit = m_job->approve_gas;
                m_tx_approve.max_priority_fee_per_gas
                    = m_job->fees.max_priority_fee_per_gas;
                m_tx_approve.max_fee_per_gas = m_job->fees.max_fee_per_gas;
            }
            m_tx_swap = tx::Eip1559Tx {};
            m_tx_swap.chain_id = chain.chain_id;
            m_tx_swap.to = address_bytes(m_quote.to);
            m_tx_swap.value = m_quote.value;
            m_tx_swap.data = m_quote.data;
            m_tx_swap.nonce = m_job->nonce + (m_need_approve ? 1 : 0);
            // The router's estimate plus a cushion; a revert refunds
            // the unused gas, an out-of-gas burns the whole limit.
            m_tx_swap.gas_limit = m_job->quote.gas_limit * 5 / 4;
            m_tx_swap.max_priority_fee_per_gas
                = m_job->fees.max_priority_fee_per_gas;
            m_tx_swap.max_fee_per_gas = m_job->fees.max_fee_per_gas;
            m_receive_label = "≥ "
                + units::format_units_display(
                    m_quote.to_amount_min, buy().decimals)
                + " " + buy().symbol;
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
        m_stage = Stage::Form;
    } else if (m_stage == Stage::Delivering && m_job->step.load() == 0) {
        // Failed before anything was signed: nothing happened
        // on-chain, the review screen takes another try.
        m_stage = Stage::Review;
        m_focus_pass = true;
    } else if (m_stage == Stage::Delivering) {
        m_stage = Stage::Failed; // keep m_job: hash may be displayable
        return;
    }
    m_job.reset();
}

void SwapPage::draw(GLFWwindow* window, const i18n::Catalog& tr)
{
    poll_job();

    if (m_vault.active() != m_wallet_seen) {
        m_wallet_seen = m_vault.active();
        m_job.reset();
        reset_to_form();
    }

    ImGui::Begin((std::string(tr("swap.title")) + "###swap-page").c_str());
    if (m_focus_self) {
        ImGui::SetWindowFocus();
        m_focus_self = false;
    }

    m_secret_focus = false;
    draw_form(tr);
    if (m_stage != Stage::Form)
        draw_confirm_dialog(tr);

    if (m_secret_focus != m_ime_disabled) {
        set_ime_enabled(window, !m_secret_focus);
        m_ime_disabled = m_secret_focus;
    }

    ImGui::End();
}

void SwapPage::draw_form(const i18n::Catalog& tr)
{
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float col = std::min(avail, em * 22.0f);
    const float left = ImGui::GetCursorPosX() + (avail - col) * 0.5f;
    const chains::ChainSpec& chain = selected_chain();

    kit_vspace(0.8f);
    ImGui::PushFont(nullptr, kit_snap(em * 2.1f));
    centered_text("🔁", false);
    ImGui::PopFont();
    kit_vspace(0.6f);

    // What leaves first: the amount with the sold asset as its badge.
    ImGui::SetCursorPosX(left);
    ImGui::SetNextItemWidth(col);
    bool pick_sell = false;
    const std::string sell_hint = sell().symbol + " · " + chain.name;
    kit_amount_field("##swap-amount", m_amount.data(), m_amount.size(),
        sell().symbol.c_str(), &pick_sell, sell_hint.c_str());
    if (pick_sell)
        ImGui::OpenPopup("##swap-sell-pop");
    if (kit_menu_begin("##swap-sell-pop")) {
        float menu_w = 0.0f;
        for (const Asset& a : m_assets)
            menu_w = std::max(menu_w,
                kit_menu_row_width(a.symbol.c_str(),
                    m_registry.all()[std::size_t(a.chain)].name.c_str()));
        for (int i = 0; i < int(m_assets.size()); ++i) {
            const Asset& a = m_assets[std::size_t(i)];
            const chains::ChainSpec& c = m_registry.all()[std::size_t(a.chain)];
            ImGui::PushID(i);
            if (kit_menu_item_icon(a.symbol.c_str(), a.symbol.c_str(),
                    c.name.c_str(), i == m_sell_index, menu_w)) {
                m_sell_index = i;
                if (buy().chain != a.chain || m_buy_index == i)
                    m_buy_index = first_partner(i);
            }
            ImGui::PopID();
        }
        kit_menu_end();
    }

    kit_vspace(0.35f);

    // What arrives: a same-chain asset — the receive side offers only
    // the sold asset's own chain, minus the asset itself.
    ImGui::PushFont(nullptr, kit_caption_size());
    ImGui::SetCursorPosX(left);
    ImGui::TextDisabled("%s", tr("swap.receive"));
    ImGui::PopFont();
    ImGui::SetCursorPosX(left);
    if (kit_select_begin("##swap-buy", buy().symbol.c_str(), col)) {
        for (int i = 0; i < int(m_assets.size()); ++i) {
            const Asset& a = m_assets[std::size_t(i)];
            if (a.chain != sell().chain || i == m_sell_index)
                continue;
            ImGui::PushID(i);
            if (kit_select_item(a.symbol.c_str(), i == m_buy_index))
                m_buy_index = i;
            ImGui::PopID();
        }
        kit_select_end();
    }

    kit_vspace(0.6f);

    const bool unlocked = m_vault.unlocked() && m_vault.keyd() != nullptr;
    const bool evm
        = keyd::preset_family(keyd::DerivePreset(m_vault.active_preset()))
        == keyd::ChainFamily::Eth;
    const bool pair_ok
        = m_buy_index != m_sell_index && buy().chain == sell().chain;

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
    if (unlocked && evm && !pair_ok)
        centered_caption(tr("swap.same"));

    kit_vspace(0.5f);

    const bool ready = unlocked && evm && pair_ok && m_amount[0] != '\0';
    ImGui::SetCursorPosX(left);
    ImGui::BeginDisabled(!ready || m_job != nullptr);
    if (kit_primary_button(tr("swap.review"), col))
        begin_review();
    ImGui::EndDisabled();

    if (m_stage == Stage::Form && !m_status.empty()) {
        kit_vspace(0.25f);
        error_note(m_status_is_key ? tr(m_status.c_str()) : m_status.c_str(),
            left, col);
    }
}

void SwapPage::begin_review()
{
    m_status.clear();
    const Asset& from_asset = sell();
    units::U256 amount;
    try {
        amount = units::parse_units(m_amount.data(), from_asset.decimals);
        if (amount.is_zero())
            throw std::runtime_error("zero");
    } catch (const std::exception&) {
        m_status = "send.err.amount";
        m_status_is_key = true;
        return;
    }
    m_pay_label = units::format_units(amount, from_asset.decimals) + " "
        + from_asset.symbol;

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
    kit_dialog_open("##swap-confirm");
    const std::string from_token
        = from_asset.token.empty() ? swap::kNativeToken : from_asset.token;
    const std::string to_token
        = buy().token.empty() ? swap::kNativeToken : buy().token;
    std::thread([job, spec = selected_chain(), from = m_from, from_token,
                    to_token, amount, sell_token = from_asset.token]() {
        try {
            job->quote = swap::fetch_quote(
                spec.chain_id, from_token, to_token, amount, from, "izan");
            chains::RpcClient rpc(spec);
            if (!sell_token.empty()) {
                // Selling a token: does the router's spender already
                // hold enough allowance, or does an approval lead?
                const units::U256 allowance = assets::erc20_allowance(
                    rpc, sell_token, from, job->quote.approval_address);
                job->need_approve = allowance < amount;
            }
            job->nonce = tx::next_nonce(rpc, from);
            if (job->need_approve) {
                tx::Eip1559Tx probe;
                probe.chain_id = spec.chain_id;
                probe.to = address_bytes(sell_token);
                probe.data = assets::erc20_approve_calldata(
                    job->quote.approval_address, amount);
                job->approve_gas = tx::estimate_gas(rpc, from, probe);
            }
            job->fees = tx::quote_fees(rpc);
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        } catch (...) {
            // Anything escaping a detached thread is process death.
            job->error = "worker failed";
            job->phase.store(2);
        }
    }).detach();
}

void SwapPage::confirm_swap()
{
    if (strnlen(m_pass.data(), m_pass.size()) == 0)
        return;
    if (!m_vault.keyd()) {
        m_status = "keyd gone";
        m_status_is_key = false;
        return;
    }
    // Both transactions join the queue before the passphrase moves:
    // the pair the human reviewed is the pair that signs, approval
    // first by nonce.
    if (m_need_approve && m_proposal_approve == 0) {
        auto id = m_vault.keyd()->submit_ui(
            keyd::make_envelope(keyd::DerivePreset(m_preset), m_account,
                tx::signing_payload(m_tx_approve)));
        if (!id) {
            m_status = m_vault.keyd()->last_error();
            m_status_is_key = false;
            return;
        }
        m_proposal_approve = *id;
    }
    if (m_proposal_swap == 0) {
        auto id = m_vault.keyd()->submit_ui(
            keyd::make_envelope(keyd::DerivePreset(m_preset), m_account,
                tx::signing_payload(m_tx_swap)));
        if (!id) {
            m_status = m_vault.keyd()->last_error();
            m_status_is_key = false;
            return;
        }
        m_proposal_swap = *id;
    }

    SecureBytes pass = take_secret(m_pass);
    auto job = std::make_shared<Job>();
    m_job = job;
    m_stage = Stage::Delivering;
    m_status.clear();
    std::thread([job, keyd = m_vault.keyd(), spec = selected_chain(),
                    approve_draft = m_tx_approve, swap_draft = m_tx_swap,
                    need_approve = m_need_approve,
                    proposal_approve = m_proposal_approve,
                    proposal_swap = m_proposal_swap,
                    pass = std::move(pass)]() mutable {
        try {
            // Sign the pair first — a wrong passphrase must fail
            // before anything reaches the chain.
            std::vector<uint8_t> approve_raw;
            if (need_approve) {
                auto sig = keyd->approve(proposal_approve, pass);
                if (!sig)
                    throw std::runtime_error(keyd->last_error());
                approve_raw = tx::encode_signed(
                    approve_draft, sig->y_parity, sig->r, sig->s);
            }
            auto sig = keyd->approve(proposal_swap, pass);
            pass.reset();
            if (!sig)
                throw std::runtime_error(keyd->last_error());
            const std::vector<uint8_t> swap_raw
                = tx::encode_signed(swap_draft, sig->y_parity, sig->r, sig->s);

            job->step.store(1);
            chains::RpcClient rpc(std::move(spec));
            // Nonce order guarantees the approval executes first; both
            // ride the mempool together.
            if (need_approve)
                (void)tx::broadcast(rpc, approve_raw);
            const std::array<uint8_t, 32> hash = tx::broadcast(rpc, swap_raw);
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
        } catch (...) {
            // Anything escaping a detached thread is process death.
            job->error = "worker failed";
            job->phase.store(2);
        }
    }).detach();
}

void SwapPage::draw_confirm_dialog(const i18n::Catalog& tr)
{
    const float em = ImGui::GetFontSize();
    const float content = em * design().dialog_width;
    bool dismissed = false;
    const bool escapable = m_stage != Stage::Delivering;
    if (!kit_dialog_begin("##swap-confirm", &dismissed, escapable))
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

    switch (m_stage) {
    case Stage::Quoting: {
        kit_vspace(0.5f);
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + (content - em * 1.4f) * 0.5f);
        kit_spinner(0.7f);
        kit_vspace(0.25f);
        centered_caption(tr("swap.quoting"));
        kit_vspace(0.5f);
        if (kit_subtle_button(tr("ui.cancel"), content)) {
            cancel_flow();
            kit_dialog_close();
        }
        break;
    }
    case Stage::Review: {
        kit_dialog_header_icon(
            "🔁", m_pay_label.c_str(), m_receive_label.c_str());

        const uint64_t total_gas = m_tx_swap.gas_limit
            + (m_need_approve ? m_tx_approve.gas_limit : 0);
        const units::U256 fee_max
            = m_tx_swap.max_fee_per_gas.checked_mul_u64(total_gas);
        row(tr("send.from"), m_vault.active_name() + " · " + m_from);
        row(tr("swap.route"), m_quote.tool + " · " + chain.name);
        row(tr("send.fee_max"),
            "≤ " + units::format_units_display(fee_max, chain.decimals) + " "
                + chain.symbol);
        if (m_need_approve)
            centered_caption(tr("swap.approve_note"));

        char plumbing[128];
        std::snprintf(plumbing, sizeof plumbing,
            "nonce %llu · gas %llu · %s gwei",
            (unsigned long long)(m_need_approve ? m_tx_approve.nonce
                                                : m_tx_swap.nonce),
            (unsigned long long)total_gas,
            units::format_units(m_tx_swap.max_fee_per_gas, 9).c_str());
        centered_caption(plumbing);
        kit_vspace(0.4f);

        if (m_focus_pass) {
            kit_focus_here();
            m_focus_pass = false;
        }
        kit_dialog_field_width();
        const bool submitted = secret_field(
            "##swap-pass", m_pass, m_secret_focus, tr("send.passphrase"));
        if (!m_status.empty()) {
            error_note(
                m_status_is_key ? tr(m_status.c_str()) : m_status.c_str(),
                ImGui::GetCursorPosX(), content);
        }
        const bool has_pass = strnlen(m_pass.data(), m_pass.size()) > 0;
        const int choice
            = kit_dialog_buttons(tr("ui.cancel"), tr("swap.confirm"), has_pass);
        if (choice == 1) {
            cancel_flow();
            kit_dialog_close();
        } else if (choice == 2 || (submitted && has_pass)) {
            confirm_swap();
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
            kit_copy_text_centered("##swap-hash", m_job->tx_hash.c_str(),
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
            ImGui::PushFont(nullptr, kit_snap(em * 1.7f));
            centered_text(m_receive_label.c_str(), false);
            ImGui::PopFont();
            centered_caption(ok ? tr("swap.confirmed") : tr("swap.reverted"));
            char block[64];
            std::snprintf(block, sizeof block, "%s %llu", tr("send.block"),
                (unsigned long long)m_job->block);
            centered_caption(block);
        } else {
            centered_caption(m_status.c_str());
        }
        if (m_job && !m_job->tx_hash.empty()) {
            kit_vspace(0.25f);
            kit_copy_text_centered("##swap-hash-done", m_job->tx_hash.c_str(),
                tr("send.hash"), tr("ui.copied"));
            if (!chain.explorer.empty()) {
                const std::string url
                    = chain.explorer + "/tx/" + m_job->tx_hash;
                const std::string label = kit_elide_middle(
                    url.c_str(), content, kit_caption_size());
                centered_caption(""); // spacing beat before the link
                const float w = ImGui::CalcTextSize(label.c_str()).x;
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() + (content - w) * 0.5f);
                kit_hyperlink("##swap-explorer", label.c_str(), url.c_str());
            }
        }
        kit_vspace(0.4f);
        if (kit_subtle_button(tr("ui.back"), content)) {
            m_job.reset();
            if (m_stage == Stage::Done)
                sodium_memzero(m_amount.data(), m_amount.size());
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
