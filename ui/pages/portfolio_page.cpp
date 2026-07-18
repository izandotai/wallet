#include "ui/pages/portfolio_page.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

#include <imgui.h>

#include "core/units/decimal.hpp"
#include "domain/assets/prices.hpp"
#include "domain/config/config_trust.hpp"
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

    // "$1,234.56" — two decimals, thousands grouped. Fiat is a read of
    // the moment, not an accounting figure; two decimals is honest.
    std::string format_usd(double v)
    {
        char raw[64];
        std::snprintf(raw, sizeof raw, "%.2f", v);
        const std::string s(raw);
        const auto dot = s.find('.');
        std::string out;
        for (std::size_t i = 0; i < dot; ++i) {
            if (i && (dot - i) % 3 == 0)
                out += ',';
            out += s[i];
        }
        return "$" + out + s.substr(dot);
    }

}

PortfolioPage::PortfolioPage(
    const std::filesystem::path& data_dir, VaultPage& vault)
    : m_vault(vault)
{
    const std::string chainsJson = slurp(data_dir / "chains.json");
    const std::string tokensJson = slurp(data_dir / "tokens.json");
    m_config_modified = config::classify("chains.json", chainsJson)
            != config::Trust::ShippedDefault
        || config::classify("tokens.json", tokensJson)
            != config::Trust::ShippedDefault;

    m_reader = std::make_shared<assets::PortfolioReader>(
        chains::ChainRegistry::from_json(chainsJson),
        assets::TokenRegistry::from_json(tokensJson));
}

void PortfolioPage::refresh(const std::string& address)
{
    if (address.empty() || m_job)
        return;
    m_status.clear();
    auto job = std::make_shared<Job>();
    m_job = job;
    // The reader is single-driver: the refresh control stays disabled
    // until the worker reports back.
    auto reader = m_reader;
    std::thread([job, reader, address] {
        try {
            for (const auto& h : reader->snapshot(address)) {
                Row row;
                row.chain = h.chain;
                row.symbol = h.symbol;
                row.ok = h.ok;
                if (h.ok)
                    row.amount = units::format_units(h.amount, h.decimals);
                else
                    row.error = h.error;
                job->rows.push_back(std::move(row));
            }
            // Fiat is garnish over the on-chain numbers: a price feed
            // failure leaves the dollar column empty and says nothing.
            try {
                std::vector<std::string> ids;
                for (const Row& row : job->rows) {
                    const std::string id = assets::coingecko_id(row.symbol);
                    if (row.ok && !id.empty()
                        && std::find(ids.begin(), ids.end(), id) == ids.end())
                        ids.push_back(id);
                }
                const auto prices = assets::fetch_usd_prices(ids);
                double total = 0.0;
                bool any = false;
                for (Row& row : job->rows) {
                    const std::string id = assets::coingecko_id(row.symbol);
                    const auto hit = prices.find(id);
                    if (!row.ok || hit == prices.end())
                        continue;
                    double amount = 0.0;
                    const auto [end, ec] = std::from_chars(row.amount.data(),
                        row.amount.data() + row.amount.size(), amount);
                    if (ec != std::errc())
                        continue;
                    const double worth = amount * hit->second;
                    row.fiat = format_usd(worth);
                    total += worth;
                    any = true;
                }
                if (any)
                    job->total = format_usd(total);
            } catch (const std::exception&) {
            }
            job->phase.store(1);
        } catch (const std::exception& e) {
            job->error = e.what();
            job->phase.store(2);
        }
    }).detach();
}

void PortfolioPage::draw(const i18n::Catalog& tr)
{
    if (m_job) {
        const int phase = m_job->phase.load();
        if (phase == 1) {
            m_rows = std::move(m_job->rows);
            m_total = std::move(m_job->total);
            m_fetched_at = ImGui::GetTime();
            m_status.clear();
            m_job.reset();
        } else if (phase == 2) {
            if (m_job->error.find("address") != std::string::npos) {
                m_status = "portfolio.err.address";
                m_status_is_key = true;
            } else {
                m_status = m_job->error;
                m_status_is_key = false;
            }
            m_job.reset();
        }
    }

    ImGui::Begin(
        (std::string(tr("portfolio.title")) + "###portfolio-page").c_str());
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool busy = m_job != nullptr;

    if (m_config_modified) {
        kit_caption(tr("portfolio.warn.config"));
        kit_vspace(0.25f);
    }

    // Follow the vault: the active account's holdings, unasked.
    const std::string mine
        = m_vault.unlocked() ? m_vault.active_address() : std::string();
    if (mine != m_followed) {
        m_followed = mine;
        m_rows.clear();
        m_total.clear();
        m_status.clear();
        m_fetched_at = 0.0;
        refresh(mine);
    }

    // Who, then the one number that matters: identity stacked on the
    // center axis, total worth as the hero line.
    if (!m_vault.active_name().empty()) {
        kit_vspace(0.5f);
        kit_identity(m_vault.active_name().c_str(),
            mine.empty() ? nullptr : mine.c_str(),
            m_total.empty() ? nullptr : m_total.c_str());
    }

    // Control line, centered under the identity: refresh with the age
    // of the numbers beside it.
    kit_vspace(0.25f);
    if (busy) {
        const float r = em * 0.55f;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + (avail - r * 2.0f) * 0.5f);
        kit_spinner(0.55f);
    } else if (!m_followed.empty()) {
        char ago[32] = "";
        if (m_fetched_at > 0.0) {
            const int age = int(ImGui::GetTime() - m_fetched_at);
            if (age < 60)
                std::snprintf(ago, sizeof ago, "%ds", age);
            else
                std::snprintf(ago, sizeof ago, "%dm", age / 60);
        }
        const float line_w = ImGui::CalcTextSize(tr("portfolio.refresh")).x
            + ImGui::GetStyle().FramePadding.x * 2.0f
            + (*ago ? ImGui::CalcTextSize(ago).x
                        + ImGui::GetStyle().ItemSpacing.x
                    : 0.0f);
        const float slack = avail - line_w;
        if (slack > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack * 0.5f);
        if (kit_link_button(tr("portfolio.refresh")))
            refresh(m_followed);
        if (*ago) {
            ImGui::SameLine();
            kit_caption(ago);
        }
    }

    if (!m_status.empty()) {
        kit_vspace(0.25f);
        kit_caption(m_status_is_key ? tr(m_status.c_str()) : m_status.c_str());
    }

    if (!m_rows.empty()) {
        kit_vspace(0.5f);
        kit_group_begin("##holdings");
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            if (i)
                kit_hairline();
            const std::string id = row.chain + "/" + row.symbol;
            kit_asset_row(id.c_str(), row.symbol.c_str(), row.chain.c_str(),
                row.amount.c_str(), row.ok, tr("portfolio.unreadable"),
                row.fiat.c_str());
        }
        kit_group_end();
    } else if (!busy && m_status.empty()) {
        kit_vspace(1.5f);
        kit_empty_state("💼", tr("portfolio.empty"));
    }

    ImGui::End();
}

}
