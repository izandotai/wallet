#include "ui/pages/history_page.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include <imgui.h>

#include "core/units/decimal.hpp"
#include "domain/assets/history.hpp"
#include "ui/widgets/kit.hpp"

namespace izan::ui {

namespace {

    std::string slurp(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot read " + path.string());
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string moment_of(uint64_t unix_seconds)
    {
        const std::chrono::sys_seconds tp { std::chrono::seconds(
            unix_seconds) };
        return std::format("{:%m-%d %H:%M}", tp);
    }

}

HistoryPage::HistoryPage(
    const std::filesystem::path& data_dir, VaultPage& vault)
    : m_registry(
          chains::ChainRegistry::from_json(slurp(data_dir / "chains.json")))
    , m_vault(vault)
{
}

void HistoryPage::refresh(const std::string& address)
{
    if (address.empty() || m_job)
        return;
    m_status.clear();
    auto job = std::make_shared<Job>();
    job->address = address;
    m_job = job;
    std::thread([job, address, chains = m_registry.all()]() {
        try {
            struct Tagged {
                assets::TxRecord rec;
                const chains::ChainSpec* chain;
            };
            std::vector<Tagged> merged;
            std::set<std::string> token_hashes;
            // Chains answer independently; one silent instance must
            // not blank the others' ledgers. Token transfers land
            // first so their hashes can silence the empty native
            // shells of the same transactions.
            for (const chains::ChainSpec& chain : chains) {
                if (chain.history.empty())
                    continue;
                try {
                    assets::Ledger ledger
                        = assets::fetch_ledger(chain, address);
                    for (assets::TxRecord& rec : ledger.tokens) {
                        token_hashes.insert(rec.hash);
                        merged.push_back({ std::move(rec), &chain });
                    }
                    for (assets::TxRecord& rec : ledger.native) {
                        // A token send's outer transaction is a
                        // zero-value call on the contract; the
                        // transfer row already tells the story.
                        if (rec.value.to_dec() == "0"
                            && token_hashes.contains(rec.hash))
                            continue;
                        merged.push_back({ std::move(rec), &chain });
                    }
                } catch (...) {
                    // One silent chain must not blank the others.
                }
            }
            std::sort(merged.begin(), merged.end(),
                [](const Tagged& a, const Tagged& b) {
                    return a.rec.time > b.rec.time;
                });
            if (merged.size() > 50)
                merged.resize(50);
            for (const Tagged& t : merged) {
                Row row;
                row.hash = t.rec.hash;
                row.counterparty = t.rec.counterparty;
                row.incoming = t.rec.incoming;
                row.failed = t.rec.failed;
                row.note = t.chain->name + " · " + moment_of(t.rec.time);
                const bool token = !t.rec.token_symbol.empty();
                row.amount = (t.rec.incoming ? "+" : "−")
                    + units::format_units_display(t.rec.value,
                        token ? t.rec.token_decimals : t.chain->decimals)
                    + " " + (token ? t.rec.token_symbol : t.chain->symbol);
                if (!t.chain->explorer.empty())
                    row.link = t.chain->explorer + "/tx/" + row.hash;
                job->rows.push_back(std::move(row));
            }
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        } catch (...) {
            // Anything escaping a detached thread is process death;
            // nothing that flies here is worth the whole wallet.
            job->error = "history worker failed";
            job->phase.store(2);
        }
    }).detach();
}

void HistoryPage::draw(const i18n::Catalog& tr)
{
    if (m_job && m_job->phase.load() != 0) {
        const int phase = m_job->phase.load();
        const bool current = m_job->address == m_followed;
        if (phase == 1 && current) {
            m_rows = std::move(m_job->rows);
            m_fetched_at = ImGui::GetTime();
            m_status.clear();
        } else if (phase == 2 && current) {
            m_status = m_job->error;
        }
        m_job.reset();
        // The account switched while this page of ledger flew; chase
        // the address now on screen.
        if (!current && !m_followed.empty())
            refresh(m_followed);
    }

    ImGui::Begin(
        (std::string(tr("history.title")) + "###history-page").c_str());
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool busy = m_job != nullptr;

    const std::string mine
        = m_vault.unlocked() ? m_vault.active_address() : std::string();
    if (mine != m_followed) {
        m_followed = mine;
        m_rows.clear();
        m_status.clear();
        m_fetched_at = 0.0;
        refresh(mine);
    }

    // Controls on one centered line, same grammar as the assets page.
    kit_vspace(0.25f);
    auto centered_x = [&](float item_w) {
        const float slack = avail - item_w;
        if (slack > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack * 0.5f);
    };
    if (busy) {
        centered_x(em * 1.1f);
        kit_spinner(0.55f);
    } else if (!m_followed.empty()) {
        centered_x(kit_button_width(tr("portfolio.refresh")));
        if (kit_link_button(tr("portfolio.refresh")))
            refresh(m_followed);
        if (m_fetched_at > 0.0) {
            const int age = int(ImGui::GetTime() - m_fetched_at);
            char ago[32];
            if (age < 60)
                std::snprintf(ago, sizeof ago, "%ds", age);
            else
                std::snprintf(ago, sizeof ago, "%dm", age / 60);
            ImGui::PushFont(nullptr, kit_caption_size());
            centered_x(ImGui::CalcTextSize(ago).x);
            ImGui::TextDisabled("%s", ago);
            ImGui::PopFont();
        }
    }

    if (!m_status.empty()) {
        kit_vspace(0.25f);
        const float inset = em * 1.1f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset);
        kit_footnote(m_status.c_str(), avail - inset * 2.0f);
    }

    if (!m_rows.empty()) {
        kit_vspace(0.5f);
        kit_group_begin("##ledger");
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            if (i)
                kit_hairline();
            if (kit_tx_row(row.hash.c_str(), row.incoming,
                    row.counterparty.c_str(), row.note.c_str(),
                    row.amount.c_str(), row.failed)
                && !row.link.empty())
                kit_open_url(row.link.c_str());
            if (ImGui::IsItemHovered())
                kit_tooltip(row.hash.c_str());
        }
        kit_group_end();
    } else if (!busy && m_status.empty()) {
        kit_vspace(1.5f);
        kit_empty_state("🧾", tr("history.empty"));
    }

    ImGui::End();
}

}
