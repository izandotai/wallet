#include "ui/pages/send_page.hpp"

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

    void copyable_line(const char* label, const std::string& text)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text.c_str());
        if (ImGui::IsItemClicked())
            ImGui::SetClipboardText(text.c_str());
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
        // Failed before anything was signed (wrong passphrase, keyd
        // refusal): nothing happened on-chain, back to the approval
        // screen for another try.
        m_stage = Stage::Approving;
    } else if (m_stage == Stage::Delivering) {
        m_stage = Stage::Failed; // keep m_job: hash may be displayable
        return;
    }
    m_job.reset();
}

void SendPage::draw(GLFWwindow* window, const i18n::Catalog& tr)
{
    poll_job();

    ImGui::Begin((std::string(tr("send.title")) + "###send-page").c_str());

    switch (m_stage) {
    case Stage::Form:
        draw_form(tr);
        break;
    case Stage::Quoting:
        ImGui::TextUnformatted(tr("send.quoting"));
        break;
    case Stage::Review:
        draw_review(tr);
        break;
    case Stage::Approving:
        draw_approving(tr);
        break;
    case Stage::Delivering:
        draw_delivering(tr);
        break;
    case Stage::Done:
    case Stage::Failed:
        draw_done(tr);
        break;
    }

    if (!m_status.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "%s", m_status_is_key ? tr(m_status.c_str()) : m_status.c_str());
    }

    // Same red line as the vault page: no IME while the passphrase
    // field can have focus.
    const bool secret_active
        = ImGui::GetIO().WantTextInput && m_stage == Stage::Approving;
    if (secret_active != m_ime_disabled) {
        set_ime_enabled(window, !secret_active);
        m_ime_disabled = secret_active;
    }

    ImGui::End();
}

void SendPage::draw_form(const i18n::Catalog& tr)
{
    if (ImGui::BeginCombo(tr("send.chain"), selected_chain().name.c_str())) {
        for (int i = 0; i < int(m_registry.all().size()); ++i) {
            if (ImGui::Selectable(m_registry.all()[std::size_t(i)].name.c_str(),
                    i == m_chain_index))
                m_chain_index = i;
        }
        ImGui::EndCombo();
    }
    ImGui::InputText(tr("send.to"), m_to.data(), m_to.size());
    ImGui::InputText(tr("send.amount"), m_amount.data(), m_amount.size());

    const bool unlocked = m_vault.unlocked() && m_vault.keyd() != nullptr;
    // Only the EVM family has a transaction engine so far; a wallet
    // wearing a BTC or Solana preset receives on that chain but cannot
    // spend from here yet.
    const bool evm
        = keyd::preset_family(keyd::DerivePreset(m_vault.active_preset()))
        == keyd::ChainFamily::Eth;
    const bool ready = unlocked && evm;
    if (!unlocked)
        ImGui::TextDisabled("%s", tr("send.state.locked"));
    else if (!evm)
        ImGui::TextDisabled("%s", tr("send.err.family"));

    ImGui::BeginDisabled(!ready || m_job != nullptr);
    if (ImGui::Button(tr("send.review"))) {
        m_status.clear();
        m_to_checked = crypto::eth_checksum_address(m_to.data());
        if (m_to_checked.empty()) {
            m_status = "send.err.address";
            m_status_is_key = true;
        } else {
            try {
                m_tx = tx::Eip1559Tx {};
                m_tx.chain_id = selected_chain().chain_id;
                m_tx.to = address_bytes(m_to_checked);
                m_tx.value = units::parse_units(
                    m_amount.data(), selected_chain().decimals);
            } catch (const std::exception&) {
                m_status = "send.err.amount";
                m_status_is_key = true;
            }
        }
        if (m_status.empty()) {
            // The sender address is a quick keyd round trip; fetch it
            // on this thread so the job never touches keyd while form
            // buttons are alive. Account index and derivation preset
            // are captured here — the quote, the envelope and the
            // signature all speak for the same identity even if the
            // selection changes later.
            m_account = m_vault.active_account();
            m_preset = m_vault.active_preset();
            auto from = m_vault.keyd()->address(m_account, m_preset);
            if (!from) {
                m_status = m_vault.keyd()->last_error();
                m_status_is_key = false;
            } else {
                m_from = *from;
                auto job = std::make_shared<Job>();
                m_job = job;
                m_stage = Stage::Quoting;
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
        }
    }
    ImGui::EndDisabled();
}

void SendPage::draw_review(const i18n::Catalog& tr)
{
    const chains::ChainSpec& chain = selected_chain();
    const std::string amount
        = units::format_units(m_tx.value, chain.decimals) + " " + chain.symbol;
    const units::U256 fee_max
        = m_tx.max_fee_per_gas.checked_mul_u64(m_tx.gas_limit);
    const std::string total
        = units::format_units(m_tx.value.checked_add(fee_max), chain.decimals)
        + " " + chain.symbol;

    ImGui::TextDisabled("%s", tr("send.from"));
    ImGui::SameLine();
    ImGui::TextUnformatted(m_from.c_str());
    ImGui::TextDisabled("%s", tr("send.to"));
    ImGui::SameLine();
    ImGui::TextUnformatted(m_to_checked.c_str());
    ImGui::TextDisabled("%s", tr("send.amount"));
    ImGui::SameLine();
    ImGui::TextUnformatted(amount.c_str());
    ImGui::Text("%s %llu   %s %llu", tr("send.nonce"),
        (unsigned long long)m_tx.nonce, tr("send.gas"),
        (unsigned long long)m_tx.gas_limit);
    ImGui::Text("%s %s gwei", tr("send.fee_max"),
        units::format_units(m_tx.max_fee_per_gas, 9).c_str());
    ImGui::TextDisabled("%s", tr("send.total_max"));
    ImGui::SameLine();
    ImGui::TextUnformatted(total.c_str());

    if (ImGui::Button(tr("send.confirm"))) {
        std::optional<uint64_t> id;
        if (m_vault.keyd()) {
            // Envelope v2: preset and account index travel inside the
            // queued bytes, so the identity the human approves — down
            // to its derivation dialect — is the identity that signs.
            id = m_vault.keyd()->submit_ui(
                keyd::make_envelope(keyd::DerivePreset(m_preset), m_account,
                    tx::signing_payload(m_tx)));
        }
        if (!id) {
            m_status = m_vault.keyd() ? m_vault.keyd()->last_error()
                                      : std::string("keyd gone");
            m_status_is_key = false;
        } else {
            m_proposal = *id;
            m_status.clear();
            m_stage = Stage::Approving;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("send.back")))
        reset_to_form();
}

void SendPage::draw_approving(const i18n::Catalog& tr)
{
    ImGui::Text(
        "%s #%llu", tr("send.proposal"), (unsigned long long)m_proposal);
    ImGui::TextDisabled("%s", tr("send.to"));
    ImGui::SameLine();
    ImGui::TextUnformatted(m_to_checked.c_str());

    ImGui::InputText(tr("send.passphrase"), m_pass.data(), m_pass.size(),
        ImGuiInputTextFlags_Password);

    const bool busy = m_job != nullptr;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(tr("send.approve"))) {
        const std::size_t len = strnlen(m_pass.data(), m_pass.size());
        SecureBytes pass(len);
        if (len)
            std::memcpy(pass.data(), m_pass.data(), len);
        sodium_memzero(m_pass.data(), m_pass.size());

        auto job = std::make_shared<Job>();
        m_job = job;
        m_stage = Stage::Delivering;
        m_status.clear();
        // The job borrows keyd for the one approve round trip; send
        // buttons are disabled while it runs, so this page keeps the
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
    ImGui::SameLine();
    if (ImGui::Button(tr("send.deny"))) {
        if (m_vault.keyd())
            m_vault.keyd()->deny(m_proposal);
        reset_to_form();
    }
    ImGui::EndDisabled();
}

void SendPage::draw_delivering(const i18n::Catalog& tr)
{
    const int step = m_job ? m_job->step.load() : 0;
    ImGui::TextUnformatted(step == 0 ? tr("send.signing")
            : step == 1              ? tr("send.broadcasting")
                                     : tr("send.waiting"));
    if (step >= 2 && m_job)
        copyable_line(tr("send.hash"), m_job->tx_hash);
}

void SendPage::draw_done(const i18n::Catalog& tr)
{
    if (m_stage == Stage::Done && m_job) {
        ImGui::TextUnformatted(
            m_job->tx_success ? tr("send.confirmed") : tr("send.reverted"));
        ImGui::Text(
            "%s %llu", tr("send.block"), (unsigned long long)m_job->block);
    } else {
        ImGui::TextUnformatted(tr("send.failed"));
    }
    if (m_job && !m_job->tx_hash.empty()) {
        copyable_line(tr("send.hash"), m_job->tx_hash);
        const std::string& explorer = selected_chain().explorer;
        if (!explorer.empty())
            copyable_line("", explorer + "/tx/" + m_job->tx_hash);
    }
    if (ImGui::Button(tr("send.back"))) {
        m_job.reset();
        reset_to_form();
    }
}

}
