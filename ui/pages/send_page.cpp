#include "ui/pages/send_page.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include <imgui.h>

#include <sodium.h>

#include "core/codec/abi.hpp"
#include "core/crypto/eth.hpp"
#include "core/crypto/sol.hpp"
#include "core/units/decimal.hpp"
#include "domain/btc/btc_tx.hpp"
#include "domain/chains/rpc_client.hpp"
#include "domain/sol/sol_tx.hpp"
#include "domain/sol/solana.hpp"
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
        // The dialog auto-sizes to its widest line: a caption drawn at
        // natural width drags the window wide while every field stays
        // pinned to the column. Wrap at the column, never the window.
        const float content = ImGui::GetFontSize() * design().dialog_width;
        ImGui::PushFont(nullptr, kit_caption_size());
        if (ImGui::CalcTextSize(text).x <= content) {
            centered_text(text, true);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + content);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
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

SendPage::SendPage(const std::filesystem::path& data_dir,
    const std::filesystem::path& user_dir, VaultPage& vault)
    : m_registry(
          chains::ChainRegistry::from_json(load_file(data_dir / "chains.json")))
    , m_vault(vault)
{
    // The spendable menu: every chain's native coin, then its
    // configured tokens — choosing an asset chooses the chain with it.
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
        if (chain.family == "sol") {
            // Whole-coin sends only; SPL tokens are a later verse.
            m_assets.push_back({ i, chain.symbol, "", chain.decimals });
            continue;
        }
        if (chain.family == "btc") {
            m_assets.push_back({ i, chain.symbol, "", chain.decimals });
            continue;
        }
        if (!chain.is_evm())
            continue;
        m_assets.push_back({ i, chain.symbol, "", chain.decimals });
        for (const assets::TokenSpec* token : tokens.tokens_for(chain.chain_id))
            m_assets.push_back(
                { i, token->symbol, token->address, token->decimals });
    }
}

SendPage::~SendPage()
{
    sodium_memzero(m_pass.data(), m_pass.size());
}

const chains::ChainSpec& SendPage::selected_chain() const
{
    return m_registry.all()[std::size_t(selected_asset().chain)];
}

void SendPage::prefill(uint64_t chain_id, const std::string& symbol,
    const std::string& token, uint8_t decimals)
{
    for (int i = 0; i < int(m_assets.size()); ++i) {
        const Asset& a = m_assets[std::size_t(i)];
        if (a.symbol == symbol && a.token == token
            && m_registry.all()[std::size_t(a.chain)].chain_id == chain_id) {
            m_asset_index = i;
            m_focus_self = true;
            return;
        }
    }
    // A holding the shipped menu never knew — an SPL mint from the
    // assets page — becomes a session asset on first use.
    if (!token.empty())
        for (int i = 0; i < int(m_registry.all().size()); ++i)
            if (m_registry.all()[std::size_t(i)].chain_id == chain_id) {
                m_assets.push_back({ i, symbol, token, decimals });
                m_asset_index = int(m_assets.size()) - 1;
                break;
            }
    m_focus_self = true;
}

void SendPage::reset_to_form()
{
    sodium_memzero(m_pass.data(), m_pass.size());
    m_tx = tx::Eip1559Tx {};
    m_sol_send = false;
    m_sol_lamports = 0;
    m_sol_msg.clear();
    m_spl_send = false;
    m_spl_mint.clear();
    m_spl_amount = 0;
    m_spl_ata_missing = false;
    m_spl_token2022 = false;
    m_btc_send = false;
    m_btc_amount = 0;
    m_btc_sel = {};
    m_btc_sel_err.clear();
    m_tx_hash_expect.clear();
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
            m_sol_blockhash = m_job->blockhash;
            m_sol_balance = m_job->balance;
            m_sol_to_balance = m_job->to_balance;
            m_sol_rent = m_job->rent;
            if (m_btc_send) {
                m_btc_utxos = std::move(m_job->utxos);
                m_btc_tiers = m_job->tiers;
                btc_reselect();
            }
            m_spl_ata_missing = m_job->ata_missing;
            m_spl_token2022 = m_job->token2022;
            m_spl_rent = m_job->token_rent;
            m_stage = Stage::Review;
            m_focus_pass = true;
            m_job.reset();
            return;
        }
        // The receipt screen keeps m_job and this branch re-runs every
        // frame — the settle bell may ring only on the way IN, or the
        // read-only pages get staleness-marked forever and spin.
        const bool arriving = m_stage != Stage::Done;
        m_stage = Stage::Done;
        if (arriving && m_on_settled)
            m_on_settled();
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
    if (m_focus_self) {
        ImGui::SetWindowFocus();
        m_focus_self = false;
    }

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
    // People paste the recipient first, while the asset menu still
    // shows its default — so the paste gate accepts any family's
    // address (base58 and 0x-hex share no alphabet), and a Solana
    // address flips the asset to Solana by itself.
    bool sol_asset = chain.family == "sol";
    bool btc_asset = chain.family == "btc";
    kit_address_field("##send-to", tr("send.to"), m_to.data(), m_to.size(),
        tr("ui.paste"), tr("ui.copy_action"), tr("ui.clear"),
        [](const char* s) {
            return sol::valid_address(s) || btc::valid_address(s)
                || !crypto::eth_checksum_address(s).empty();
        });

    const bool to_present = m_to[0] != '\0';
    // A pasted address knows its own family (the three alphabets are
    // disjoint) and flips the asset itself.
    auto flip_to_family = [&](const char* fam, bool& flag) {
        for (int i = 0; i < int(m_assets.size()); ++i)
            if (m_registry.all()[std::size_t(m_assets[std::size_t(i)].chain)]
                    .family
                == fam) {
                m_asset_index = i;
                flag = true;
                break;
            }
    };
    if (to_present && !sol_asset && sol::valid_address(m_to.data()))
        flip_to_family("sol", sol_asset);
    else if (to_present && !btc_asset && btc::valid_address(m_to.data()))
        flip_to_family("btc", btc_asset);
    const bool to_valid = to_present
        && (sol_asset ? sol::valid_address(m_to.data())
                : btc_asset
                ? btc::valid_address(m_to.data())
                : !crypto::eth_checksum_address(m_to.data()).empty());
    if (to_present && !to_valid)
        centered_caption(tr("send.err.address"));

    kit_vspace(0.35f);

    // The amount row carries the asset as its badge — the coin (or
    // token) being counted, with its network. Picking from the menu
    // picks both at once.
    const Asset& asset = selected_asset();
    ImGui::SetCursorPosX(left);
    ImGui::SetNextItemWidth(col);
    bool pick_asset = false;
    const std::string badge_hint = asset.symbol + " · " + chain.name;
    kit_amount_field("##send-amount", m_amount.data(), m_amount.size(),
        asset.symbol.c_str(), &pick_asset, badge_hint.c_str());
    if (pick_asset)
        ImGui::OpenPopup("##send-asset-pop");
    if (kit_menu_begin("##send-asset-pop")) {
        // One width for every row, or the highlights come out ragged.
        float menu_w = 0.0f;
        for (const Asset& a : m_assets)
            menu_w = std::max(menu_w,
                kit_menu_row_width(a.symbol.c_str(),
                    m_registry.all()[std::size_t(a.chain)].name.c_str()));
        for (int i = 0; i < int(m_assets.size()); ++i) {
            const Asset& a = m_assets[std::size_t(i)];
            const chains::ChainSpec& c = m_registry.all()[std::size_t(a.chain)];
            // The same symbol lives on many chains; the row index is
            // the identity, the label is just the face.
            ImGui::PushID(i);
            if (kit_menu_item_icon(a.symbol.c_str(), a.symbol.c_str(),
                    c.name.c_str(), i == m_asset_index, menu_w))
                m_asset_index = i;
            ImGui::PopID();
        }
        kit_menu_end();
    }

    kit_vspace(0.6f);

    const bool unlocked = m_vault.unlocked() && m_vault.keyd() != nullptr;
    // The wallet must have a self on the asset own family: an
    // all-chain seed has all of them, a key wallet only its own.
    const std::string sender = m_vault.family_address(sol_asset ? "sol"
            : btc_asset                                         ? "btc"
                                                                : "evm");
    // Every BTC costume spends now — legacy, nested, native, taproot.
    const bool can_send = !sender.empty();
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
    if (unlocked && !can_send)
        centered_caption(tr("send.err.family"));

    kit_vspace(0.5f);

    const bool ready = unlocked && can_send && to_valid && m_amount[0] != '\0';
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
    const Asset& asset = selected_asset();
    m_sol_send = selected_chain().family == "sol";
    m_btc_send = selected_chain().family == "btc";
    if (m_sol_send) {
        m_spl_send = !asset.token.empty();
        m_spl_mint = asset.token;
        m_spl_decimals = asset.decimals;
        begin_sol_review();
        return;
    }
    if (m_btc_send) {
        begin_btc_review();
        return;
    }
    m_to_checked = crypto::eth_checksum_address(m_to.data());
    try {
        m_tx = tx::Eip1559Tx {};
        m_tx.chain_id = selected_chain().chain_id;
        const units::U256 amount
            = units::parse_units(m_amount.data(), asset.decimals);
        if (asset.token.empty()) {
            m_tx.to = address_bytes(m_to_checked);
            m_tx.value = amount;
        } else {
            // A token send is a call on the token contract; the human's
            // recipient rides inside the calldata, reviewed and signed
            // as part of exactly these bytes.
            m_tx.to = address_bytes(asset.token);
            m_tx.data = codec::CallData("transfer(address,uint256)")
                            .add_address(m_to_checked)
                            .add_u256(amount)
                            .to_bytes();
        }
        m_amount_label
            = units::format_units(amount, asset.decimals) + " " + asset.symbol;
        m_token_send = !asset.token.empty();
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
        } catch (...) {
            // Anything escaping a detached thread is process death.
            job->error = "worker failed";
            job->phase.store(2);
        }
    }).detach();
}

void SendPage::begin_sol_review()
{
    const Asset& asset = selected_asset();
    std::string to(m_to.data());
    while (!to.empty() && (to.back() == ' ' || to.back() == '\n'))
        to.pop_back();
    m_to_checked = to;
    try {
        const units::U256 amount
            = units::parse_units(m_amount.data(), asset.decimals);
        const std::string dec = amount.to_dec();
        m_sol_lamports = 0;
        const auto res = std::from_chars(
            dec.data(), dec.data() + dec.size(), m_sol_lamports);
        if (res.ec != std::errc() || res.ptr != dec.data() + dec.size()
            || m_sol_lamports == 0)
            throw std::invalid_argument("amount");
        m_amount_label
            = units::format_units(amount, asset.decimals) + " " + asset.symbol;
        m_token_send = false;
    } catch (const std::exception&) {
        m_status = "send.err.amount";
        m_status_is_key = true;
        return;
    }
    m_account = m_vault.active_account();
    m_preset = m_vault.family_preset_value("sol");
    auto from = m_vault.keyd()->address(m_account, m_preset);
    if (!from) {
        m_status = m_vault.keyd()->last_error();
        m_status_is_key = false;
        return;
    }
    m_from = *from;
    if (m_spl_send) {
        m_spl_amount = m_sol_lamports;
        m_sol_lamports = 0;
    }
    auto job = std::make_shared<Job>();
    m_job = job;
    m_stage = Stage::Quoting;
    kit_dialog_open("##send-confirm");
    std::thread([job, spec = selected_chain(), from = m_from, to = m_to_checked,
                    spl = m_spl_send, mint = m_spl_mint]() mutable {
        try {
            chains::RpcClient rpc(std::move(spec));
            job->blockhash = sol::latest_blockhash(rpc);
            const std::string bal = sol::native_balance(rpc, from).to_dec();
            std::from_chars(bal.data(), bal.data() + bal.size(), job->balance);
            if (spl) {
                // The lamports only pay fee and, if the recipient's
                // door is closed, the rent to open it. The mint's own
                // program decides where that door even is.
                job->token2022 = sol::mint_is_token2022(rpc, mint);
                job->token_rent = sol::rent_exempt_minimum(rpc, 165);
                job->ata_missing = !sol::account_exists(
                    rpc, crypto::sol_ata(to, mint, job->token2022));
            } else {
                const std::string tb = sol::native_balance(rpc, to).to_dec();
                std::from_chars(
                    tb.data(), tb.data() + tb.size(), job->to_balance);
                job->rent = sol::rent_exempt_minimum(rpc);
            }
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        } catch (...) {
            job->error = "worker failed";
            job->phase.store(2);
        }
    }).detach();
}

void SendPage::confirm_sol_send()
{
    // The quote's guard rails, checked with the figures on screen:
    // Solana refuses dust below the rent floor, so refuse it here
    // before any signature exists. Verbatim messages — node-grade
    // plumbing errors, not yet in the phrasebook.
    constexpr uint64_t kFeeLamports = 5000;
    if (m_spl_send) {
        // Token units travel on their own ledger; the lamports only
        // pay the fee and, when the door is closed, the rent.
        const uint64_t need
            = kFeeLamports + (m_spl_ata_missing ? m_spl_rent : 0);
        if (m_sol_balance < need) {
            m_status = "SOL balance cannot cover the fee"
                       " and the recipient's account rent";
            m_status_is_key = false;
            return;
        }
    } else {
        if (m_sol_balance < m_sol_lamports + kFeeLamports) {
            m_status = "balance cannot cover amount plus the 0.000005 SOL fee";
            m_status_is_key = false;
            return;
        }
        // A self-transfer's money comes home; only the fee leaves, so
        // the remainder rules judge balance minus fee alone.
        const bool self = m_from == m_to_checked;
        const uint64_t left = self
            ? m_sol_balance - kFeeLamports
            : m_sol_balance - m_sol_lamports - kFeeLamports;
        if (left != 0 && left < m_sol_rent) {
            m_status = "remainder would fall below the rent floor; "
                       "send less, or everything";
            m_status_is_key = false;
            return;
        }
        if (!self && m_sol_to_balance == 0 && m_sol_lamports < m_sol_rent) {
            m_status = "recipient is a fresh account; the amount must "
                       "cover its rent floor";
            m_status_is_key = false;
            return;
        }
    }
    if (m_proposal == 0) {
        try {
            m_sol_msg = m_spl_send
                ? sol::encode_spl_transfer(m_from, m_to_checked, m_spl_mint,
                      m_spl_amount, m_spl_decimals, m_sol_blockhash,
                      m_spl_token2022)
                : sol::encode_transfer_message(
                      m_from, m_to_checked, m_sol_lamports, m_sol_blockhash);
        } catch (const std::exception& e) {
            m_status = e.what();
            m_status_is_key = false;
            return;
        }
        auto id = m_vault.keyd()->submit_ui(keyd::make_envelope(
            keyd::DerivePreset(m_preset), m_account, m_sol_msg));
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
    std::thread([job, keyd = m_vault.keyd(), spec = selected_chain(),
                    msg = m_sol_msg, proposal = m_proposal,
                    pass = std::move(pass)]() mutable {
        try {
            auto sig = keyd->approve_sol(proposal, pass);
            pass.reset();
            if (!sig)
                throw std::runtime_error(keyd->last_error());
            const std::vector<uint8_t> raw = sol::assemble_tx(*sig, msg);
            job->step.store(1);
            chains::RpcClient rpc(std::move(spec));
            job->tx_hash = sol::send_transaction(rpc, raw);
            job->step.store(2);
            // A blockhash lives ~90 seconds; two minutes of polling
            // covers the whole window plus the node's digestion.
            for (int tries = 0; tries < 60; ++tries) {
                const sol::SigStatus st
                    = sol::signature_status(rpc, job->tx_hash);
                if (st == sol::SigStatus::Confirmed
                    || st == sol::SigStatus::Finalized) {
                    job->tx_success = true;
                    job->phase.store(1);
                    return;
                }
                if (st == sol::SigStatus::Failed) {
                    job->tx_success = false;
                    job->phase.store(1);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            throw std::runtime_error("no confirmation after 2 minutes; "
                                     "check the explorer before retrying");
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        } catch (...) {
            job->error = "worker failed";
            job->phase.store(2);
        }
    }).detach();
}

void SendPage::btc_reselect()
{
    // A tier change reprices the whole selection; a new proposal will
    // be built from whatever stands when the human confirms.
    const uint64_t rate = m_btc_tier == 0 ? m_btc_tiers.fast
        : m_btc_tier == 2                 ? m_btc_tiers.slow
                                          : m_btc_tiers.normal;
    m_btc_sel = {};
    m_btc_sel_err.clear();
    m_proposal = 0;
    m_tx_hash_expect.clear();
    // Each costume weighs differently on the wire; outputs are
    // costed at the heaviest common script so the fee never under-
    // shoots for a taproot recipient.
    const uint64_t in_vb = m_preset == uint8_t(keyd::DerivePreset::BtcLegacy)
        ? btc::kVbP2pkh
        : m_preset == uint8_t(keyd::DerivePreset::BtcNestedSegwit)
        ? btc::kVbP2shP2wpkh
        : m_preset == uint8_t(keyd::DerivePreset::BtcTaproot) ? btc::kVbP2tr
                                                              : btc::kVbP2wpkh;
    try {
        m_btc_sel
            = btc::select_coins(m_btc_utxos, m_btc_amount, rate, in_vb, 43);
    } catch (const std::exception& e) {
        m_btc_sel_err = e.what();
    }
}

void SendPage::begin_btc_review()
{
    const Asset& asset = selected_asset();
    std::string to(m_to.data());
    while (!to.empty() && (to.back() == ' ' || to.back() == '\n'))
        to.pop_back();
    m_to_checked = to;
    try {
        const units::U256 amount
            = units::parse_units(m_amount.data(), asset.decimals);
        const std::string dec = amount.to_dec();
        m_btc_amount = 0;
        const auto res = std::from_chars(
            dec.data(), dec.data() + dec.size(), m_btc_amount);
        if (res.ec != std::errc() || res.ptr != dec.data() + dec.size())
            throw std::invalid_argument("amount");
        // The network refuses to relay outputs below the dust bound —
        // an amount that cannot travel deserves its own explanation.
        if (m_btc_amount < btc::kDustSats) {
            m_status = "send.err.dust";
            m_status_is_key = true;
            return;
        }
        m_amount_label
            = units::format_units(amount, asset.decimals) + " " + asset.symbol;
        m_token_send = false;
    } catch (const std::exception&) {
        m_status = "send.err.amount";
        m_status_is_key = true;
        return;
    }
    m_account = m_vault.active_account();
    m_preset = m_vault.family_preset_value("btc");
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
    std::thread([job, spec = selected_chain(), from = m_from]() mutable {
        try {
            job->utxos = btc::fetch_utxos(spec, from);
            job->tiers = btc::fee_tiers(spec);
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        } catch (...) {
            job->error = "worker failed";
            job->phase.store(2);
        }
    }).detach();
}

void SendPage::confirm_btc_send()
{
    if (!m_btc_sel_err.empty() || m_btc_sel.inputs.empty()) {
        m_status = m_btc_sel_err.empty() ? "no coins selected" : m_btc_sel_err;
        m_status_is_key = false;
        return;
    }
    if (m_proposal == 0) {
        // The plan: picked coins in, the recipient, and — when there
        // is change — our own address to bring it home.
        btc::TxPlan plan;
        std::vector<uint8_t> values;
        try {
            for (const btc::Utxo& u : m_btc_sel.inputs) {
                btc::TxPlan::In in;
                if (u.txid.size() != 64)
                    throw std::invalid_argument("btc: malformed txid");
                for (int i = 0; i < 32; ++i) {
                    auto nib = [](char c) {
                        return uint8_t(c <= '9' ? c - '0' : c - 'a' + 10);
                    };
                    in.txid_be[std::size_t(i)]
                        = uint8_t(nib(u.txid[std::size_t(2 * i)]) << 4
                            | nib(u.txid[std::size_t(2 * i + 1)]));
                }
                in.vout = u.vout;
                plan.inputs.push_back(in);
                for (int b = 0; b < 8; ++b)
                    values.push_back(uint8_t(u.value >> (8 * b)));
            }
            btc::TxPlan::Out pay;
            pay.value = m_btc_amount;
            pay.script = btc::script_for_address(m_to_checked);
            plan.outputs.push_back(std::move(pay));
            if (m_btc_sel.change > 0) {
                btc::TxPlan::Out change;
                change.value = m_btc_sel.change;
                change.script = btc::script_for_address(m_from);
                plan.outputs.push_back(std::move(change));
            }
        } catch (const std::exception& e) {
            m_status = e.what();
            m_status_is_key = false;
            return;
        }
        const auto skel = btc::encode_skeleton(plan);
        std::vector<uint8_t> payload(4);
        payload[0] = uint8_t(skel.size());
        payload[1] = uint8_t(skel.size() >> 8);
        payload[2] = uint8_t(skel.size() >> 16);
        payload[3] = uint8_t(skel.size() >> 24);
        payload.insert(payload.end(), skel.begin(), skel.end());
        payload.insert(payload.end(), values.begin(), values.end());
        auto id = m_vault.keyd()->submit_ui(keyd::make_envelope(
            keyd::DerivePreset(m_preset), m_account, payload));
        if (!id) {
            m_status = m_vault.keyd()->last_error();
            m_status_is_key = false;
            return;
        }
        m_proposal = *id;
        // The reviewed txid, computed before any signature exists —
        // the broadcast leg must see the node echo exactly this.
        m_tx_hash_expect = btc::txid_of(plan);
    }
    SecureBytes pass = take_secret(m_pass);
    auto job = std::make_shared<Job>();
    m_job = job;
    m_stage = Stage::Delivering;
    m_status.clear();
    std::thread([job, keyd = m_vault.keyd(), spec = selected_chain(),
                    proposal = m_proposal, expect = m_tx_hash_expect,
                    pass = std::move(pass)]() mutable {
        try {
            auto tx = keyd->approve_btc(proposal, pass);
            pass.reset();
            if (!tx)
                throw std::runtime_error(keyd->last_error());
            job->step.store(1);
            job->tx_hash = btc::broadcast_tx(spec, *tx, expect);
            job->step.store(2);
            // Accepted into the mempool is the receipt this dialog
            // waits for — a block is ten minutes away on a good day,
            // and the ledger page will show the confirmation.
            job->tx_success = true;
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        } catch (...) {
            job->error = "worker failed";
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
    if (m_sol_send) {
        confirm_sol_send();
        return;
    }
    if (m_btc_send) {
        confirm_btc_send();
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
        } catch (...) {
            // Anything escaping a detached thread is process death.
            job->error = "worker failed";
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
    const std::string& amount = m_amount_label;

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

        row(tr("send.from"), m_vault.active_name() + " · " + m_from);
        row(tr("send.to"), m_to_checked);
        if (m_btc_send) {
            // The fee market in three tiers; switching repriced the
            // coin selection before this frame drew it.
            const char* tier_keys[3]
                = { "send.fee.fast", "send.fee.normal", "send.fee.slow" };
            const uint64_t tier_rates[3]
                = { m_btc_tiers.fast, m_btc_tiers.normal, m_btc_tiers.slow };
            ImGui::PushFont(nullptr, kit_caption_size());
            float rowW = 0;
            std::string labels[3];
            for (int i = 0; i < 3; ++i) {
                labels[std::size_t(i)] = std::string(tr(tier_keys[i])) + " "
                    + std::to_string(tier_rates[i]);
                rowW += kit_button_width(labels[std::size_t(i)].c_str())
                    + ImGui::GetStyle().ItemSpacing.x;
            }
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - rowW) * 0.5f);
            for (int i = 0; i < 3; ++i) {
                ImGui::PushID(200 + i);
                const bool on = m_btc_tier == i;
                if ((on ? kit_primary_button(labels[std::size_t(i)].c_str())
                        : kit_subtle_button(labels[std::size_t(i)].c_str()))
                    && !on) {
                    m_btc_tier = i;
                    btc_reselect();
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::PopFont();
            ImGui::NewLine();
            if (m_btc_sel_err.empty()) {
                row(tr("send.fee_max"),
                    units::format_units_display(
                        units::U256::from_u64(m_btc_sel.fee), chain.decimals)
                        + " " + chain.symbol);
                row(tr("send.total_max"),
                    units::format_units_display(
                        units::U256::from_u64(m_btc_amount + m_btc_sel.fee),
                        chain.decimals)
                        + " " + chain.symbol);
                char coins[96];
                std::snprintf(coins, sizeof coins, "%zu in · change %s",
                    m_btc_sel.inputs.size(),
                    m_btc_sel.change
                        ? units::format_units_display(
                              units::U256::from_u64(m_btc_sel.change),
                              chain.decimals)
                              .c_str()
                        : "0");
                centered_caption(coins);
            } else {
                error_note(
                    m_btc_sel_err.c_str(), ImGui::GetCursorPosX(), content);
            }
        } else if (m_sol_send) {
            // Solana's fee is a flat per-signature price, exact; for
            // whole-coin sends the sum is honest. A token send keeps
            // its units apart and declares any door-opening rent.
            constexpr uint64_t kFeeLamports = 5000;
            const uint64_t fee = kFeeLamports
                + (m_spl_send && m_spl_ata_missing ? m_spl_rent : 0);
            row(tr("send.fee_max"),
                units::format_units_display(
                    units::U256::from_u64(fee), chain.decimals)
                    + " " + chain.symbol);
            if (m_spl_send) {
                if (m_spl_ata_missing)
                    centered_caption(tr("send.rent"));
            } else {
                row(tr("send.total_max"),
                    units::format_units_display(
                        units::U256::from_u64(m_sol_lamports + kFeeLamports),
                        chain.decimals)
                        + " " + chain.symbol);
            }
        } else {
            const units::U256 fee_max
                = m_tx.max_fee_per_gas.checked_mul_u64(m_tx.gas_limit);
            const std::string fee
                = units::format_units_display(fee_max, chain.decimals) + " "
                + chain.symbol;
            row(tr("send.fee_max"), "≤ " + fee);
            // A native send can honestly sum coin and fee; a token send
            // cannot — the amount and the fee live in different units.
            if (!m_token_send) {
                const std::string total
                    = units::format_units_display(
                          m_tx.value.checked_add(fee_max), chain.decimals)
                    + " " + chain.symbol;
                row(tr("send.total_max"), "≤ " + total);
            }
            char plumbing[128];
            std::snprintf(plumbing, sizeof plumbing,
                "nonce %llu · gas %llu · %s gwei",
                (unsigned long long)m_tx.nonce,
                (unsigned long long)m_tx.gas_limit,
                units::format_units_display(m_tx.max_fee_per_gas, 9).c_str());
            centered_caption(plumbing);
        }
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
            // Solana's receipt is the signature's fate; Bitcoin's is
            // mempool acceptance — neither owns a block number here.
            if (!m_sol_send && !m_btc_send) {
                char block[64];
                std::snprintf(block, sizeof block, "%s %llu", tr("send.block"),
                    (unsigned long long)m_job->block);
                centered_caption(block);
            }
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
