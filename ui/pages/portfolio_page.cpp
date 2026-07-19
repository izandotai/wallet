#include "ui/pages/portfolio_page.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include <glaze/glaze.hpp>
#include <imgui.h>

#include "core/crypto/eth.hpp"
#include "core/units/decimal.hpp"
#include "domain/assets/prices.hpp"
#include "domain/btc/esplora.hpp"
#include "domain/config/config_trust.hpp"
#include "domain/sol/solana.hpp"
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

    // The user token file's own row shape — a plain mirror of
    // tokens.json entries, written back pretty so the file stays
    // hand-editable.
    struct UserTokenRow {
        uint64_t chain_id {};
        std::string address;
        std::string symbol;
        uint8_t decimals {};
    };

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
    : m_data_dir(data_dir)
    , m_user_dir(user_dir)
    , m_vault(vault)
{
    rebuild_reader();
}

void PortfolioPage::rebuild_reader()
{
    const std::string chainsJson = slurp(m_data_dir / "chains.json");
    const std::string tokensJson = slurp(m_data_dir / "tokens.json");
    m_config_modified = config::classify("chains.json", chainsJson)
            != config::Trust::ShippedDefault
        || config::classify("tokens.json", tokensJson)
            != config::Trust::ShippedDefault;

    chains::ChainRegistry chains = chains::ChainRegistry::from_json(chainsJson);
    m_explorers.clear();
    for (const chains::ChainSpec& spec : chains.all())
        m_explorers[spec.chain_id] = spec.explorer;
    m_chains = chains.all();
    assets::TokenRegistry tokens = assets::TokenRegistry::from_json(tokensJson);
    // The person's own tokens ride a separate file, outside the
    // shipped set and its digest — absent or malformed, the shipped
    // set stands alone.
    m_user_tokens.clear();
    try {
        const assets::TokenRegistry user = assets::TokenRegistry::from_json(
            slurp(m_user_dir / "tokens.user.json"));
        for (const assets::TokenSpec& t : user.all())
            m_user_tokens.emplace_back(t.chain_id, t.address);
        tokens.extend(user, chains);
    } catch (const std::exception&) {
    }
    m_known = tokens.all();

    m_reader = std::make_shared<assets::PortfolioReader>(
        std::move(chains), std::move(tokens));
}

void PortfolioPage::refresh(const std::array<std::string, 3>& addrs)
{
    if ((addrs[0].empty() && addrs[1].empty() && addrs[2].empty()) || m_job)
        return;
    m_status.clear();
    auto job = std::make_shared<Job>();
    job->address = addrs[0] + "|" + addrs[1] + "|" + addrs[2];
    m_job = job;
    // The reader is single-driver: the refresh control stays disabled
    // until the worker reports back.
    auto reader = m_reader;
    const bool want_prices = ImGui::GetTime() - m_priced_at > 60.0;
    std::vector<chains::ChainSpec> btc_chains, sol_chains;
    for (const chains::ChainSpec& c : m_chains) {
        if (c.family == "btc")
            btc_chains.push_back(c);
        else if (c.family == "sol")
            sol_chains.push_back(c);
    }
    std::thread([job, reader, addrs, want_prices,
                    btc_chains = std::move(btc_chains),
                    sol_chains = std::move(sol_chains), cache = m_prices] {
        try {
            // A number lands as a row through one dresser, whatever
            // engine read it.
            auto add_row = [&](Row row, const units::U256& amount,
                               uint8_t decimals) {
                row.ok = true;
                row.decimals = decimals;
                const std::string full = units::format_units(amount, decimals);
                std::from_chars(
                    full.data(), full.data() + full.size(), row.approx);
                row.amount = units::format_units_display(amount, decimals);
                job->rows.push_back(std::move(row));
            };
            // Whole-coin rows through their family engines — an
            // all-chain wallet fills all of them, a single-family one
            // brings just its own address.
            auto native_row = [&](const chains::ChainSpec& spec,
                                  const std::string& address, bool is_sol) {
                Row row;
                row.chain_id = spec.chain_id;
                row.chain = spec.name;
                row.symbol = spec.symbol;
                row.testnet = spec.testnet;
                try {
                    if (is_sol) {
                        chains::RpcClient rpc(spec);
                        add_row(row, sol::native_balance(rpc, address),
                            spec.decimals);
                        // The SPL shelf, same breath: majors by our
                        // table, strangers by their own on-chain card
                        // (sanitized — that text is author-controlled),
                        // and only then by a shortened mint. Empty
                        // ATAs stay off the screen.
                        for (const sol::SplHolding& h :
                            sol::token_accounts(rpc, address)) {
                            if (h.amount == 0)
                                continue;
                            Row t;
                            t.chain_id = spec.chain_id;
                            t.chain = spec.name;
                            std::string sym = sol::known_mint_symbol(h.mint);
                            if (sym.empty())
                                try {
                                    const sol::MintMeta meta = sol::mint_meta(
                                        rpc, h.mint, h.token2022);
                                    sym = meta.symbol.empty() ? meta.name
                                                              : meta.symbol;
                                } catch (const std::exception&) {
                                    // nameless stranger: address it is
                                }
                            t.symbol = sym.empty() ? h.mint.substr(0, 4) + "…"
                                    + h.mint.substr(h.mint.size() - 4)
                                                   : sym;
                            t.token = h.mint;
                            t.testnet = spec.testnet;
                            add_row(std::move(t),
                                units::U256::from_u64(h.amount), h.decimals);
                        }
                    } else {
                        add_row(row, btc::native_balance(spec, address),
                            spec.decimals);
                    }
                } catch (const std::exception& e) {
                    row.error = e.what();
                    job->rows.push_back(std::move(row));
                }
            };
            if (!addrs[1].empty())
                for (const chains::ChainSpec& spec : btc_chains)
                    native_row(spec, addrs[1], false);
            if (!addrs[2].empty())
                for (const chains::ChainSpec& spec : sol_chains)
                    native_row(spec, addrs[2], true);
            if (!addrs[0].empty()) {
                for (const auto& h : reader->snapshot(addrs[0])) {
                    Row row;
                    row.chain_id = h.chain_id;
                    row.chain = h.chain;
                    row.symbol = h.symbol;
                    row.token = h.token;
                    row.testnet = h.testnet;
                    if (h.ok) {
                        add_row(std::move(row), h.amount, h.decimals);
                    } else {
                        row.error = h.error;
                        job->rows.push_back(std::move(row));
                    }
                }
            }
            // Per-row fiat is garnish over the on-chain numbers: a
            // price feed failure leaves the dollar column empty and
            // says nothing. Testnet rows never get a figure — test
            // money priced as real money is a lie — and no total is
            // computed at all: every dollar shown is independently
            // checkable against its own row.
            job->prices = cache;
            std::vector<std::string> ids;
            for (const Row& row : job->rows) {
                const std::string id = assets::coingecko_id(row.symbol);
                if (row.ok && !row.testnet && !id.empty()
                    && std::find(ids.begin(), ids.end(), id) == ids.end())
                    ids.push_back(id);
            }
            // The minute throttle yields when the cache cannot price
            // this page at all — switching families brings symbols the
            // last wallet never fetched, and a missing dollar column
            // is worse than one extra polite request.
            bool need_fetch = want_prices;
            for (const std::string& id : ids)
                if (!need_fetch && !cache.contains(id))
                    need_fetch = true;
            if (need_fetch) {
                try {
                    // Merge, don't replace: the cache keeps pricing
                    // the wallets this page is not looking at.
                    for (const auto& [id, usd] : assets::fetch_usd_prices(ids))
                        job->prices[id] = usd;
                    job->priced = true;
                } catch (const std::exception&) {
                    // Rate-limited or down: yesterday's price beats a
                    // blank column; the cache stands in.
                    job->prices = cache;
                }
            }
            for (Row& row : job->rows) {
                if (!row.ok || row.testnet)
                    continue;
                const auto hit
                    = job->prices.find(assets::coingecko_id(row.symbol));
                if (hit == job->prices.end())
                    continue;
                row.fiat = format_usd(row.approx * hit->second);
            }
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
            if (m_job->priced) {
                m_prices = std::move(m_job->prices);
                m_priced_at = m_fetched_at;
            }
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
            refresh(m_addrs);
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

    // Follow the vault: the active account's holdings, unasked —
    // watch-only wallets included; reading needs no keys. Each family
    // is probed with its own face of the identity: a Solana address
    // never meets eth_calls, nor the reverse — an all-chain wallet
    // simply brings all of its faces at once.
    const std::array<std::string, 3> mine = { m_vault.family_address("evm"),
        m_vault.family_address("btc"), m_vault.family_address("sol") };
    const std::string key
        = mine[0].empty() && mine[1].empty() && mine[2].empty()
        ? std::string()
        : mine[0] + "|" + mine[1] + "|" + mine[2];
    // Costume flips inside the receive dialog hold still here; the
    // key changes once, when the dialog closes.
    if (key != m_followed && !m_vault.follow_frozen()) {
        m_followed = key;
        m_addrs = mine;
        m_rows.clear();
        m_status.clear();
        m_fetched_at = 0.0;
        refresh(mine);
    }

    // Identity belongs to an unlocked wallet only — a locked page has
    // nothing to introduce, so it shows the empty state alone. The
    // header wears the wallet's own line; the other faces live in the
    // receive QR and the per-row explorer links.
    const std::string face = m_vault.followed_address();
    if (!face.empty()) {
        kit_vspace(0.5f);
        kit_identity(m_vault.active_name().c_str(), face.c_str(), nullptr, 2.4f,
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
            refresh(m_addrs);
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
            // The row's verbs follow its family: send/swap/tokens are
            // EVM engines; a Solana row offers reading only.
            const chains::ChainSpec* rspec = nullptr;
            for (const chains::ChainSpec& c : m_chains)
                if (c.chain_id == row.chain_id)
                    rspec = &c;
            const bool row_evm = rspec && rspec->is_evm();
            ImGui::PushID(int(i));
            const AssetRowEvent ev = kit_asset_row(id.c_str(),
                row.symbol.c_str(), row.chain.c_str(), row.amount.c_str(),
                row.ok, tr("portfolio.unreadable"), row.fiat.c_str(), true);
            const bool row_spl
                = rspec && rspec->family == "sol" && !row.token.empty();
            // The row's token rides along whatever the family — an
            // ERC-20 contract and an SPL mint both name the asset the
            // send form should preselect; natives carry the empty
            // string. (Gating the token on the SPL flag once silenced
            // every EVM token row: symbol+empty-token matches nothing,
            // so the click focused the form and selected nothing.)
            if (ev.clicked && row.ok && (row_evm || row_spl) && m_on_send)
                m_on_send(row.chain_id, row.symbol, row.token, row.decimals);
            if (ev.hovered && row.ok && (row_evm || row_spl))
                kit_tooltip(tr("send.title"));
            if (ev.menu)
                ImGui::OpenPopup("##asset-menu");
            if (kit_menu_begin("##asset-menu")) {
                if ((row_evm || row_spl)
                    && kit_menu_item(tr("send.title"), nullptr, false, row.ok)
                    && m_on_send)
                    m_on_send(
                        row.chain_id, row.symbol, row.token, row.decimals);
                if (row_evm
                    && kit_menu_item(tr("swap.title"), nullptr, false, row.ok)
                    && m_on_swap)
                    m_on_swap(row.chain_id, row.symbol);
                if (!row.token.empty()
                    && kit_menu_item(tr("asset.menu.contract")))
                    ImGui::SetClipboardText(row.token.c_str());
                // Two explorer doors, named for what they open: the
                // token's own page (global view of the contract) and
                // my address's page (my balances and transfers). One
                // vague "view on explorer" conflated them.
                const auto ex = m_explorers.find(row.chain_id);
                const bool has_ex
                    = ex != m_explorers.end() && !ex->second.empty();
                if (has_ex && !row.token.empty()
                    && kit_menu_item(tr("asset.menu.token.explorer")))
                    kit_open_url((ex->second + "/token/" + row.token).c_str());
                // "My address" scopes to the row: for a token it opens
                // the token page filtered to MY address — the scan
                // family's native ?a= grammar (all majors here run
                // scan-family explorers); a blockscout that ignores
                // ?a= still lands on the token page, nothing breaks.
                // Only the chain's own coin opens the plain address
                // page.
                // Each family's explorer speaks its own path dialect:
                // solscan says /account/, the scan family /address/.
                const char* addr_path = rspec && rspec->family == "sol"
                    ? "/account/"
                    : "/address/";
                // The row's family picks which face of the identity
                // the explorer should be asked about.
                const std::string& row_addr = rspec && rspec->family == "sol"
                    ? m_addrs[2]
                    : rspec && rspec->family == "btc" ? m_addrs[1]
                                                      : m_addrs[0];
                if (has_ex && kit_menu_item(tr("asset.menu.addr.explorer")))
                    kit_open_url(
                        (row.token.empty() ? ex->second + addr_path + row_addr
                                           : ex->second + "/token/" + row.token
                                    + "?a=" + row_addr)
                            .c_str());
                // Removal is offered only for the user's own rows —
                // the shipped set is config under digest, not a menu
                // casualty.
                const bool user_owned = !row.token.empty()
                    && std::find(m_user_tokens.begin(), m_user_tokens.end(),
                           std::pair<uint64_t, std::string>(
                               row.chain_id, row.token))
                        != m_user_tokens.end();
                // Removal asks first — a row menu is one slip away
                // from the wrong click.
                if (user_owned && kit_menu_item(tr("asset.menu.remove"))) {
                    m_remove_chain = row.chain_id;
                    m_remove_token = row.token;
                    m_remove_symbol = row.symbol;
                    m_open_remove = true;
                }
                kit_menu_end();
            }
            ImGui::PopID();
        }
        kit_group_end();
    } else if (!busy && m_status.empty()) {
        kit_vspace(1.5f);
        kit_empty_state("💼", tr("portfolio.empty"));
    }

    // The remove confirmation, armed by the row menu above. Removing
    // only forgets the list entry; the chain is not consulted and the
    // holding itself is untouched — the dialog says exactly that.
    if (m_open_remove) {
        kit_dialog_open("##remove-token");
        m_open_remove = false;
    }
    if (kit_dialog_begin("##remove-token")) {
        kit_dialog_header_avatar(m_remove_symbol.c_str(),
            m_remove_symbol.c_str(), tr("asset.remove.warn"));
        // The centered copy-text elides to the dialog's width; a bare
        // 42-char caption would stretch the window past the button row
        // and shove the whole card off its center axis.
        kit_copy_text_centered("##rm-addr", m_remove_token.c_str(),
            tr("ui.copy"), tr("ui.copied"));
        kit_vspace(0.2f);
        const int choice = kit_dialog_buttons(
            tr("ui.cancel"), tr("asset.menu.remove"), true, true);
        if (choice == 2)
            remove_user_token(m_remove_chain, m_remove_token);
        if (choice != 0)
            kit_dialog_close();
        kit_dialog_end();
    }

    // The door to the user's own token list — only when a wallet is
    // on stage; a locked page has nothing to add to. (`key` is the
    // joined faces, empty when every family is; the array itself is
    // never "empty" — std::array::empty() answers for the type.)
    if (!key.empty()) {
        kit_vspace(0.35f);
        centered_x(kit_button_width(tr("portfolio.addtoken")));
        if (kit_link_button(tr("portfolio.addtoken"))) {
            m_add_status.clear();
            kit_dialog_open("##add-token");
        }
        draw_add_token(tr);
    }

    ImGui::End();
}

void PortfolioPage::remove_user_token(
    uint64_t chain_id, const std::string& address)
{
    std::vector<UserTokenRow> rows;
    {
        std::ifstream f(m_user_dir / "tokens.user.json", std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            std::vector<UserTokenRow> parsed;
            if (!glz::read<glz::opts { .error_on_unknown_keys = false }>(
                    parsed, ss.str()))
                rows = std::move(parsed);
        }
    }
    std::erase_if(rows, [&](const UserTokenRow& r) {
        return r.chain_id == chain_id && r.address == address;
    });
    std::string out;
    if (!glz::write<glz::opts { .prettify = true }>(rows, out)) {
        std::ofstream f(m_user_dir / "tokens.user.json",
            std::ios::binary | std::ios::trunc);
        f << out;
    }
    rebuild_reader();
    // Same trick as adding: an emptied follow re-runs the follow logic
    // next frame — rows clear, a refresh fires, in-flight snapshots
    // are chased.
    m_followed.clear();
}

void PortfolioPage::draw_add_token(const i18n::Catalog& tr)
{
    if (m_probe && m_probe->phase.load() != 0) {
        if (m_probe->phase.load() == 1) {
            m_add_preview = m_probe->found;
            m_preview_chain = m_probe->chain_id;
            m_preview_addr = m_probe->address;
            m_add_status.clear();
        } else {
            m_add_preview.reset();
            m_add_status = m_probe->error;
        }
        m_probe.reset();
    }

    bool dismissed = false;
    if (!kit_dialog_begin("##add-token", &dismissed))
        return;
    if (dismissed) {
        m_add_preview.reset();
        m_add_status.clear();
    }
    kit_dialog_header_icon("💰", tr("addtoken.title"), tr("addtoken.sub"));

    // ERC-20 is an EVM concept; other families never appear here.
    if (m_add_chain >= int(m_chains.size())
        || !m_chains[std::size_t(m_add_chain)].is_evm())
        m_add_chain = 0;
    const chains::ChainSpec& sel = m_chains[std::size_t(m_add_chain)];
    kit_dialog_field_width();
    if (kit_select_begin("##add-chain", sel.name.c_str())) {
        for (int i = 0; i < int(m_chains.size()); ++i) {
            if (!m_chains[std::size_t(i)].is_evm())
                continue;
            if (kit_select_item(
                    m_chains[std::size_t(i)].name.c_str(), i == m_add_chain))
                m_add_chain = i;
        }
        kit_select_end();
    }
    kit_dialog_field_width();
    kit_address_field("##add-addr", tr("addtoken.hint"), m_add_addr.data(),
        m_add_addr.size(), tr("ui.paste"), tr("ui.copy_action"), tr("ui.clear"),
        [](const char* s) { return !crypto::eth_checksum_address(s).empty(); });

    const std::string checked = crypto::eth_checksum_address(m_add_addr.data());
    const bool probing = m_probe != nullptr;
    // The preview only speaks for the pair it was probed on; touch
    // the chain or the address and it steps down.
    const bool preview_live = m_add_preview.has_value()
        && m_preview_chain == sel.chain_id && m_preview_addr == checked;

    kit_vspace(0.2f);
    ImGui::BeginDisabled(checked.empty() || probing);
    if (kit_subtle_button(tr("addtoken.probe"))) {
        auto job = std::make_shared<ProbeJob>();
        job->chain_id = sel.chain_id;
        job->address = checked;
        m_probe = job;
        m_add_status.clear();
        std::thread([job, spec = sel]() {
            try {
                chains::RpcClient rpc(spec);
                job->found = assets::probe_token(rpc, job->address);
                job->phase.store(1);
            } catch (const std::exception& e) {
                job->error = e.what();
                job->phase.store(2);
            } catch (...) {
                job->error = "probe failed";
                job->phase.store(2);
            }
        }).detach();
    }
    ImGui::EndDisabled();
    if (probing) {
        ImGui::SameLine();
        kit_spinner(0.55f);
    } else if (preview_live) {
        ImGui::SameLine();
        ImGui::PushFont(nullptr, kit_caption_size());
        ImGui::Text("%s · %u", m_add_preview->symbol.c_str(),
            unsigned(m_add_preview->decimals));
        ImGui::PopFont();
    }
    if (!m_add_status.empty()) {
        kit_vspace(0.15f);
        kit_footnote(
            m_add_status.c_str(), ImGui::GetFontSize() * design().dialog_width);
    }

    const int choice = kit_dialog_buttons(
        tr("ui.cancel"), tr("addtoken.add"), preview_live && !probing);
    if (choice == 2) {
        bool known = false;
        for (const assets::TokenSpec& t : m_known)
            known = known
                || (t.chain_id == m_preview_chain && t.address == checked);
        if (known) {
            m_add_status = tr("addtoken.exists");
        } else {
            // Read-modify-write the user file; a malformed existing
            // file is abandoned rather than obeyed.
            std::vector<UserTokenRow> rows;
            {
                std::ifstream f(
                    m_user_dir / "tokens.user.json", std::ios::binary);
                if (f) {
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    std::vector<UserTokenRow> parsed;
                    if (!glz::read<glz::opts {
                            .error_on_unknown_keys = false }>(parsed, ss.str()))
                        rows = std::move(parsed);
                }
            }
            rows.push_back({ m_preview_chain, checked, m_add_preview->symbol,
                m_add_preview->decimals });
            std::string out;
            if (!glz::write<glz::opts { .prettify = true }>(rows, out)) {
                std::ofstream f(m_user_dir / "tokens.user.json",
                    std::ios::binary | std::ios::trunc);
                f << out;
            }
            rebuild_reader();
            m_add_preview.reset();
            std::memset(m_add_addr.data(), 0, m_add_addr.size());
            // Emptying the followed address makes the next frame's
            // follow logic treat this as a wallet switch: rows clear,
            // a refresh fires, and the swallowed-refresh chase covers
            // a snapshot already in flight.
            m_followed.clear();
            kit_dialog_close();
        }
    } else if (choice == 1) {
        m_add_preview.reset();
        m_add_status.clear();
        kit_dialog_close();
    }
    kit_dialog_end();
}

}
