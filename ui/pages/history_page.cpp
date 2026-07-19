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
#include "platform/time/local_clock.hpp"
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

    // The row wears a terse UTC moment; the tooltip owes the user the
    // whole date in their own clock. The platform layer answers what
    // time it is there — this page only dresses the answer.
    std::string local_moment_of(uint64_t unix_seconds)
    {
        if (!unix_seconds)
            return {};
        const auto lm = time::local_moment(unix_seconds);
        if (!lm)
            return {};
        const int abs_min
            = lm->offset_min < 0 ? -lm->offset_min : lm->offset_min;
        return std::format(
            "{:04}-{:02}-{:02} {:02}:{:02}:{:02} (UTC{}{:02}:{:02})", lm->year,
            lm->month, lm->day, lm->hour, lm->minute, lm->second,
            lm->offset_min < 0 ? '-' : '+', abs_min / 60, abs_min % 60);
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
    std::vector<chains::ChainSpec> flying;
    for (const chains::ChainSpec& chain : m_registry.all())
        if (!chain.history.empty())
            flying.push_back(chain);
    if (flying.empty())
        return;
    auto job = std::make_shared<Job>();
    job->address = address;
    job->spawned = int(flying.size());
    job->pending.store(job->spawned);
    m_job = job;
    for (const chains::ChainSpec& chain : flying) {
        std::thread([job, address, chain]() {
            try {
                assets::Ledger ledger = assets::fetch_ledger(chain, address);
                std::vector<Row> local;
                std::set<std::string> token_hashes;
                // Token transfers land first so their hashes can
                // silence the empty native shells of the same
                // transactions; hashes never cross chains, so the
                // set stays chain-local.
                auto add = [&](const assets::TxRecord& rec, bool token) {
                    Row row;
                    row.hash = rec.hash;
                    row.counterparty = rec.counterparty;
                    row.incoming = rec.incoming;
                    row.failed = rec.failed;
                    row.time = rec.time;
                    row.note = chain.name + " · " + moment_of(rec.time);
                    row.when_hint = local_moment_of(rec.time);
                    row.amount = (rec.incoming ? "+" : "−")
                        + units::format_units_display(rec.value,
                            token ? rec.token_decimals : chain.decimals)
                        + " " + (token ? rec.token_symbol : chain.symbol);
                    if (!chain.explorer.empty())
                        row.link = chain.explorer + "/tx/" + row.hash;
                    local.push_back(std::move(row));
                };
                for (const assets::TxRecord& rec : ledger.tokens) {
                    token_hashes.insert(rec.hash);
                    add(rec, true);
                }
                for (const assets::TxRecord& rec : ledger.native) {
                    // A token send's outer transaction is a zero-value
                    // call on the contract; the transfer row already
                    // tells the story.
                    if (rec.value.to_dec() == "0"
                        && token_hashes.contains(rec.hash))
                        continue;
                    add(rec, false);
                }
                {
                    std::lock_guard lock(job->mu);
                    job->rows.insert(job->rows.end(),
                        std::make_move_iterator(local.begin()),
                        std::make_move_iterator(local.end()));
                }
                job->dirty.store(true);
            } catch (const std::exception& e) {
                std::lock_guard lock(job->mu);
                if (job->error.empty())
                    job->error = e.what();
                ++job->failed;
            } catch (...) {
                // Anything escaping a detached thread is process
                // death; nothing that flies here is worth the wallet.
                std::lock_guard lock(job->mu);
                if (job->error.empty())
                    job->error = "history worker failed";
                ++job->failed;
            }
            job->pending.fetch_sub(1);
        }).detach();
    }
}

void HistoryPage::draw(const i18n::Catalog& tr)
{
    if (m_job) {
        const bool current = m_job->address == m_followed;
        const bool done = m_job->pending.load() == 0;
        // Landed chains show at once; the merge is re-taken on every
        // landing and once more at the end, so the last chain's rows
        // can never slip between a dirty flag and the finish line.
        // The committed pages sit under whatever this page brings.
        if (current && (m_job->dirty.exchange(false) || done)) {
            std::vector<Row> snap;
            {
                std::lock_guard lock(m_job->mu);
                snap = m_job->rows;
            }
            std::sort(snap.begin(), snap.end(),
                [](const Row& a, const Row& b) { return a.time > b.time; });
            m_rows = std::move(snap);
        }
        if (done) {
            if (current) {
                m_fetched_at = ImGui::GetTime();
                std::lock_guard lock(m_job->mu);
                // Partial silence stays silent, as before; only a
                // ledger with nothing to say explains why.
                if (m_rows.empty() && m_job->failed == m_job->spawned)
                    m_status = m_job->error;
            }
            m_job.reset();
            // The account switched while this ledger flew; chase the
            // address now on screen.
            if (!current && !m_followed.empty())
                refresh(m_followed);
        }
    }

    ImGui::Begin(
        (std::string(tr("history.title")) + "###history-page").c_str());
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool busy = m_job != nullptr;

    // The six-chain blockscout ledger is EVM-only; other families
    // show the empty state until their own ledgers arrive.
    const std::string mine = m_vault.active_family() == keyd::ChainFamily::Eth
        ? m_vault.followed_address()
        : std::string();
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
            // The hash is not a row identity: a composite transaction
            // keeps both its token row and its native row, and a swap
            // can emit several token rows — all sharing one hash.
            ImGui::PushID(int(i));
            if (kit_tx_row(row.hash.c_str(), row.incoming,
                    row.counterparty.c_str(), row.note.c_str(),
                    row.amount.c_str(), row.failed, row.hash.c_str(),
                    row.when_hint.empty() ? nullptr : row.when_hint.c_str())
                && !row.link.empty())
                kit_open_url(row.link.c_str());
            ImGui::PopID();
        }
        kit_group_end();
    } else if (!busy && m_status.empty()) {
        kit_vspace(1.5f);
        kit_empty_state("🧾", tr("history.empty"));
    }

    ImGui::End();
}

}
