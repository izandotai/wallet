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

PortfolioPage::PortfolioPage(const std::filesystem::path& data_dir,
    const std::filesystem::path& user_dir, VaultPage& vault)
    : m_vault(vault)
{
    const std::string chainsJson = slurp(data_dir / "chains.json");
    const std::string tokensJson = slurp(data_dir / "tokens.json");
    m_config_modified = config::classify("chains.json", chainsJson)
            != config::Trust::ShippedDefault
        || config::classify("tokens.json", tokensJson)
            != config::Trust::ShippedDefault;

    chains::ChainRegistry chains = chains::ChainRegistry::from_json(chainsJson);
    assets::TokenRegistry tokens = assets::TokenRegistry::from_json(tokensJson);
    // The person's own tokens ride a separate file, outside the
    // shipped set and its digest — absent or malformed, the shipped
    // set stands alone.
    try {
        tokens.extend(assets::TokenRegistry::from_json(
                          slurp(user_dir / "tokens.user.json")),
            chains);
    } catch (const std::exception&) {
    }

    m_reader = std::make_shared<assets::PortfolioReader>(
        std::move(chains), std::move(tokens));
}

void PortfolioPage::refresh(const std::string& address)
{
    if (address.empty() || m_job)
        return;
    m_status.clear();
    auto job = std::make_shared<Job>();
    job->address = address;
    m_job = job;
    // The reader is single-driver: the refresh control stays disabled
    // until the worker reports back.
    auto reader = m_reader;
    std::thread([job, reader, address] {
        try {
            for (const auto& h : reader->snapshot(address)) {
                Row row;
                row.chain_id = h.chain_id;
                row.chain = h.chain;
                row.symbol = h.symbol;
                row.ok = h.ok;
                row.testnet = h.testnet;
                if (h.ok) {
                    // Fiat math reads the exact figure; the row shows
                    // the trimmed one — an 18-digit tail bursts rows.
                    const std::string full
                        = units::format_units(h.amount, h.decimals);
                    std::from_chars(
                        full.data(), full.data() + full.size(), row.approx);
                    row.amount
                        = units::format_units_display(h.amount, h.decimals);
                } else {
                    row.error = h.error;
                }
                job->rows.push_back(std::move(row));
            }
            // Per-row fiat is garnish over the on-chain numbers: a
            // price feed failure leaves the dollar column empty and
            // says nothing. Testnet rows never get a figure — test
            // money priced as real money is a lie — and no total is
            // computed at all: every dollar shown is independently
            // checkable against its own row.
            try {
                std::vector<std::string> ids;
                for (const Row& row : job->rows) {
                    const std::string id = assets::coingecko_id(row.symbol);
                    if (row.ok && !row.testnet && !id.empty()
                        && std::find(ids.begin(), ids.end(), id) == ids.end())
                        ids.push_back(id);
                }
                const auto prices = assets::fetch_usd_prices(ids);
                for (Row& row : job->rows) {
                    if (!row.ok || row.testnet)
                        continue;
                    const auto hit
                        = prices.find(assets::coingecko_id(row.symbol));
                    if (hit == prices.end())
                        continue;
                    row.fiat = format_usd(row.approx * hit->second);
                }
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
    if (m_job && m_job->phase.load() != 0) {
        // Only the followed address's snapshot may land — rows or
        // error; anything else speaks for an account that already
        // left the stage.
        const int phase = m_job->phase.load();
        const bool current = m_job->address == m_followed;
        if (phase == 1 && current) {
            m_rows = std::move(m_job->rows);
            m_fetched_at = ImGui::GetTime();
            m_status.clear();
        } else if (phase == 2 && current) {
            if (m_job->error.find("address") != std::string::npos) {
                m_status = "portfolio.err.address";
                m_status_is_key = true;
            } else {
                m_status = m_job->error;
                m_status_is_key = false;
            }
        }
        m_job.reset();
        // The account switched while this snapshot flew, and the
        // switch's own refresh was swallowed by the single-driver
        // gate — chase the address now on screen.
        if (!current && !m_followed.empty())
            refresh(m_followed);
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
        m_status.clear();
        m_fetched_at = 0.0;
        refresh(mine);
    }

    // Identity belongs to an unlocked wallet only — a locked page has
    // nothing to introduce, so it shows the empty state alone.
    if (!mine.empty()) {
        kit_vspace(0.5f);
        kit_identity(m_vault.active_name().c_str(), mine.c_str(), nullptr, 2.4f,
            tr("ui.copy"), tr("ui.copied"));
    }

    // Controls under the identity, each on its own centered line.
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
            if (kit_asset_row(id.c_str(), row.symbol.c_str(), row.chain.c_str(),
                    row.amount.c_str(), row.ok, tr("portfolio.unreadable"),
                    row.fiat.c_str())
                && row.ok && m_on_send)
                m_on_send(row.chain_id, row.symbol);
            if (row.ok && ImGui::IsItemHovered())
                kit_tooltip(tr("send.title"));
        }
        kit_group_end();
    } else if (!busy && m_status.empty()) {
        kit_vspace(1.5f);
        kit_empty_state("💼", tr("portfolio.empty"));
    }

    ImGui::End();
}

}
