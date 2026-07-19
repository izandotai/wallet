#include "ui/pages/vault_page.hpp"

#include <cstring>
#include <thread>
#include <vector>

#include <imgui.h>
#include <sodium.h>

#include <fstream>
#include <sstream>

#include "core/crypto/bip39.hpp"
#include "core/crypto/sol.hpp"
#include "core/secure/vault.hpp"
#include "core/units/decimal.hpp"
#include "domain/assets/balances.hpp"
#include "domain/btc/esplora.hpp"
#include "domain/chains/rpc_client.hpp"
#include "domain/sol/solana.hpp"
#include "keyd/signer.hpp"
#include "ui/shell/ime.hpp"
#include "ui/wallet/import_model.hpp"
#include "ui/wallet/presets.hpp"
#include "ui/widgets/kit.hpp"

namespace izan::ui {

using secure::SecureBytes;

VaultPage::VaultPage(std::filesystem::path wallets_dir,
    std::filesystem::path data_dir, std::string exe_path,
    std::string initial_active)
    : m_store(std::move(wallets_dir))
    , m_session(std::move(exe_path))
    , m_data_dir(std::move(data_dir))
{
    if (m_store.empty()) {
        m_mode = Mode::NoWallets;
        return;
    }
    switch_active(
        m_store.known(initial_active) ? initial_active : m_store.first_id());
}

VaultPage::~VaultPage() = default;

void VaultPage::switch_active(const std::string& id)
{
    m_session.teardown();
    m_balances.clear();
    m_bal_job.reset();
    m_create.reset();
    m_import.reset();
    m_unlock.reset();
    m_accounts.reset();
    m_secret.reset();
    m_status.clear();
    m_active = id;
    m_active_name = id;
    m_vault_path = m_store.vault_path(id);
    m_meta = m_store.read_meta(id);
    if (!m_meta.name.empty())
        m_active_name = m_meta.name;
    if (m_meta.kind == kKindWatch) {
        // Nothing to unlock: the addresses are right in the sidecar.
        m_mode = Mode::Watch;
        m_accounts.set_labels(m_meta.labels, uint32_t(m_meta.watch.size()));
        fetch_balances();
    } else {
        m_mode = std::filesystem::exists(m_vault_path) ? Mode::Locked
                                                       : Mode::NoWallets;
    }
}

void VaultPage::set_status(const char* key)
{
    m_status = key;
    m_status_is_key = true;
}

void VaultPage::enter(Mode mode)
{
    m_status.clear();
    // Screen changes land at the top of the NEXT frame. The selector
    // row is drawn before the mode switch; flipping the mode between
    // them would stack the old and new screens in one frame — a
    // visible blink every time a selector button opens a form.
    m_pending_mode = mode;
}

void VaultPage::poll_job()
{
    if (!m_job)
        return;
    const int phase = m_job->phase.load();
    if (phase == 0)
        return;
    if (phase == 1) {
        m_status.clear(); // e.g. the "unlocking…" notice has served
        if (!m_job->wallet.empty()) {
            m_store.rescan();
            switch_active(m_job->wallet);
        }
        m_mode = m_job->next;
        m_session.mark_unlocked(m_mode == Mode::Unlocked);
        // A root secret (fresh phrase, backup reveal) arms its dialog —
        // after switch_active, which resets the view it rides in.
        if (!m_job->secret.empty())
            m_secret.show(std::move(m_job->secret), m_job->secret_kind);
        if (m_mode == Mode::Unlocked) {
            // The addresses, fetched once per unlock: the first thing
            // a person needs from an unlocked wallet.
            const uint32_t actual
                = m_session.refresh_addresses(m_meta.count, m_meta.preset);
            // The all-chain books: an HD seed's other identities,
            // derived locally by keyd in the same breath.
            for (const std::string& fam : wallet_families(m_meta))
                if (fam != family_key(active_family()))
                    m_session.refresh_family(
                        fam, actual, uint8_t(family_preset(m_meta, fam)));
            bool dirty = false;
            if (actual != m_meta.count) {
                m_meta.count = actual;
                if (m_meta.active >= actual)
                    m_meta.active = 0;
                dirty = true;
            }
            // Self-heal the kind badge: keyd just told us what this
            // wallet really is; legacy sidecars learn it here.
            if (m_session.client()) {
                const char* kind = kKindSecp;
                switch (m_session.client()->wallet_kind()) {
                case keyd::RevealKind::SeedEntropy:
                    kind = kKindHd;
                    break;
                case keyd::RevealKind::Ed25519Key:
                    kind = kKindEd25519;
                    break;
                case keyd::RevealKind::PrivateKey:
                    break;
                }
                if (m_meta.kind != kind) {
                    m_meta.kind = kind;
                    dirty = true;
                }
            }
            if (dirty) {
                m_store.write_meta(m_active, m_meta);
                m_store.rescan(); // the card face shows kind and count
            }
            m_accounts.set_labels(m_meta.labels, actual);
            fetch_balances();
        }
    } else if (m_job->error.find("passphrase") != std::string::npos) {
        // Known trust-plane refusals ("bad passphrase", "wrong
        // passphrase or corrupted vault") get translated text; anything
        // unexpected stays verbatim — a raw diagnostic beats a wrong
        // translation.
        set_status("vault.err.unlock");
    } else {
        m_status = m_job->error;
        m_status_is_key = false;
    }
    m_job.reset();
}

void VaultPage::draw(GLFWwindow* window, const i18n::Catalog& tr)
{
    poll_job();
    if (m_bal_job && m_bal_job->phase.load() != 0) {
        if (m_bal_job->phase.load() == 1) {
            if (m_bal_job->index < 0) {
                m_balances = std::move(m_bal_job->out);
            } else if (!m_bal_job->out.empty()) {
                if (m_balances.size() <= std::size_t(m_bal_job->index))
                    m_balances.resize(std::size_t(m_bal_job->index) + 1);
                m_balances[std::size_t(m_bal_job->index)]
                    = std::move(m_bal_job->out.front());
            }
        }
        m_bal_job.reset();
    }
    if (m_pending_mode) {
        m_mode = *m_pending_mode;
        m_pending_mode.reset();
    }
    // A BTC-born wallet's account rows settle up once the receive
    // dialog stops flipping costumes.
    if (m_bal_stale && !m_accounts.receive_open()) {
        m_bal_stale = false;
        fetch_balances();
    }

    m_secret_focus = false; // the secret inputs below re-mark it
    const bool busy = m_job != nullptr;
    const DesignLanguage& dl = design();
    const float em = ImGui::GetFontSize();

    // Two dock windows, not two panes: the wallet list keeps its own
    // narrow column and the detail screen gets real width wherever the
    // person docks it. List events are applied after both windows have
    // drawn — a screen must never change mid-frame.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(em * dl.sidebar_pad, em * dl.sidebar_pad));
    ImGui::Begin((std::string(tr("wallet.list")) + "###wallet-list").c_str());
    ImGui::PopStyleVar();
    const WalletListView::Event lev
        = m_list.draw(tr, busy, m_store, m_active, m_session.unlocked());
    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(em * dl.pane_pad_x, em * dl.pane_pad_y));
    ImGui::Begin((std::string(tr("vault.title")) + "###vault-page").c_str());
    ImGui::PopStyleVar();

    if (m_mode == Mode::Unlocked || m_mode == Mode::Watch) {
        // The header: who this wallet is, and what it is, at a glance.
        const float avatar = em * dl.header_avatar;
        const ImVec2 head = ImGui::GetCursorScreenPos();
        const float text_left = head.x + avatar + em * 0.55f;
        const float avail_w
            = ImGui::GetContentRegionAvail().x - avatar - em * 0.55f;
        kit_avatar_at(head, m_active_name.c_str(), avatar);
        ImGui::SetCursorScreenPos(ImVec2(text_left, head.y - em * 0.1f));
        kit_title(
            kit_elide_end(m_active_name.c_str(), avail_w, kit_title_size())
                .c_str());
        ImGui::SetCursorScreenPos(ImVec2(text_left, head.y + em * 1.35f));
        const char* badge = kind_badge_key(m_meta.kind);
        if (*badge) {
            kit_pill(tr(badge), kit_accent());
            ImGui::SameLine();
        }
        if (m_meta.kind == kKindHd && avail_w > em * 12.0f) {
            kit_pill(preset_name(keyd::DerivePreset(m_meta.preset)),
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::SetCursorScreenPos(ImVec2(head.x, head.y + avatar + em * 0.6f));
    }

    switch (m_mode) {
    case Mode::NoWallets:
        kit_empty_state("👛", m_store.dir().string().c_str());
        break;
    case Mode::ImportForm:
        start_import(m_import.draw(tr, busy, m_secret_focus, m_store));
        break;
    case Mode::Locked: {
        UnlockView::Event ev
            = m_unlock.draw(tr, busy, m_secret_focus, m_active_name);
        if (ev.err)
            set_status(ev.err);
        else if (ev.type == UnlockView::Event::Type::Submit)
            start_unlock(std::move(ev.pass));
        break;
    }
    case Mode::Unlocked: {
        // The receive QR's family books, evm/btc/sol: the birth
        // family shows the session's own line, the others the books
        // fetched at unlock. Key wallets leave them empty.
        std::array<std::span<const std::string>, 3> books {};
        const std::string mine = family_key(
            keyd::preset_family(keyd::DerivePreset(m_meta.preset)));
        for (const std::string& fam : wallet_families(m_meta)) {
            const std::size_t slot = fam == "btc" ? 1 : fam == "sol" ? 2 : 0;
            if (wallet_families(m_meta).size() > 1)
                books[slot] = fam == mine
                    ? std::span<const std::string>(m_session.addresses())
                    : std::span<const std::string>(
                          m_session.family_addresses(fam));
        }
        handle_accounts(m_accounts.draw(tr, busy, m_secret_focus,
            m_session.addresses(), m_balances, m_meta.active,
            m_session.client()
                && m_session.client()->wallet_kind()
                    == keyd::RevealKind::SeedEntropy,
            false, books,
            wallet_families(m_meta).size() > 1
                ? uint8_t(family_preset(m_meta, "btc"))
                : 0));
        break;
    }
    case Mode::Watch:
        // The same account list, read-only flavor: no lock, no backup,
        // no deriving — addresses, balances, QR and copy.
        handle_accounts(m_accounts.draw(tr, busy, m_secret_focus, m_meta.watch,
            m_balances, m_meta.active, false, true));
        break;
    }

    // Dialog flows hosted by the detail window: create, and the
    // root-secret reveal that follows a create or a backup.
    if (m_open_create) {
        kit_dialog_open("##create-wallet");
        m_open_create = false;
    }
    start_create(m_create.draw_dialog(tr, busy, m_secret_focus, m_store));
    m_secret.draw_dialog(tr);

    if (!m_status.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "%s", m_status_is_key ? tr(m_status.c_str()) : m_status.c_str());
    }

    ImGui::End();

    handle_list(lev);

    // Secret fields and the IME cannot coexist: composition strings
    // are cached outside the process. Field-level, not mode-level:
    // only passphrase/secret inputs mark the frame — the wallet name
    // is ordinary text and CJK input must keep working there.
    if (m_secret_focus != m_ime_disabled) {
        set_ime_enabled(window, !m_secret_focus);
        m_ime_disabled = m_secret_focus;
    }
}

void VaultPage::fetch_balances(int only_index)
{
    const keyd::ChainFamily fam = active_family();
    if (m_bal_job)
        return; // single driver: one balance worker at a time
    const std::vector<std::string>& book
        = m_mode == Mode::Watch ? m_meta.watch : m_session.addresses();
    std::vector<std::string> addrs;
    if (only_index < 0) {
        m_balances.clear();
        addrs = book;
    } else {
        if (std::size_t(only_index) >= book.size())
            return;
        addrs.push_back(book[std::size_t(only_index)]);
    }
    auto job = std::make_shared<BalanceJob>();
    job->index = only_index;
    m_bal_job = job;
    std::thread([job, addrs = std::move(addrs), fam,
                    chains_path = m_data_dir / "chains.json"]() {
        try {
            std::ifstream f(chains_path, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            const chains::ChainRegistry registry
                = chains::ChainRegistry::from_json(ss.str());
            // The account line prices in the wallet family's first
            // chain — ETH for EVM books, SOL and BTC for their own.
            const char* fam_key = fam == keyd::ChainFamily::Sol ? "sol"
                : fam == keyd::ChainFamily::Btc                 ? "btc"
                                                                : "evm";
            const chains::ChainSpec* home = nullptr;
            for (const chains::ChainSpec& c : registry.all())
                if (c.family == fam_key) {
                    home = &c;
                    break;
                }
            if (!home)
                throw std::runtime_error("no chain for this family");
            const chains::ChainSpec& spec = *home;
            std::unique_ptr<chains::RpcClient> rpc;
            if (fam != keyd::ChainFamily::Btc)
                rpc = std::make_unique<chains::RpcClient>(spec);
            for (const std::string& addr : addrs) {
                std::string line;
                try {
                    const units::U256 wei = fam == keyd::ChainFamily::Sol
                        ? sol::native_balance(*rpc, addr)
                        : fam == keyd::ChainFamily::Btc
                        ? btc::native_balance(spec, addr)
                        : assets::native_balance(*rpc, addr);
                    line = units::format_units(wei, spec.decimals);
                    // Four decimals is garnish enough for a sidebar.
                    const auto dot = line.find('.');
                    if (dot != std::string::npos && line.size() > dot + 5)
                        line.resize(dot + 5);
                    line += " " + spec.symbol;
                } catch (const std::exception&) {
                    // An unreadable balance shows nothing, not zero.
                }
                job->out.push_back(std::move(line));
            }
            job->phase.store(1);
        } catch (const std::exception&) {
            job->phase.store(2);
        } catch (...) {
            job->phase.store(2); // never let a thread kill the wallet
        }
    }).detach();
}

void VaultPage::handle_list(WalletListView::Event ev)
{
    switch (ev.type) {
    case WalletListView::Event::Type::Activate:
        switch_active(ev.id);
        break;
    case WalletListView::Event::Type::Create:
        m_create.reset();
        m_open_create = true;
        break;
    case WalletListView::Event::Type::Import:
        m_import.reset();
        enter(Mode::ImportForm);
        break;
    case WalletListView::Event::Type::Rename: {
        std::string current;
        for (const WalletEntry& w : m_store.wallets())
            if (w.id == ev.id)
                current = w.name;
        if (ev.name == current)
            break;
        if (!m_store.valid_new_name(ev.name)) {
            set_status("wallet.err.name");
            break;
        }
        AccountsMeta meta = m_store.read_meta(ev.id);
        meta.name = ev.name;
        m_store.write_meta(ev.id, meta);
        m_store.rescan();
        if (ev.id == m_active) {
            m_meta.name = ev.name;
            m_active_name = ev.name;
        }
        break;
    }
    case WalletListView::Event::Type::Delete:
        m_store.delete_wallet(ev.id);
        if (ev.id == m_active) {
            if (m_store.empty()) {
                m_session.teardown();
                m_active.clear();
                m_active_name.clear();
                m_meta = {};
                enter(Mode::NoWallets);
            } else {
                switch_active(m_store.first_id());
            }
        }
        break;
    case WalletListView::Event::Type::None:
        break;
    }
}

void VaultPage::start_create(CreateView::Event ev)
{
    if (ev.type != CreateView::Event::Type::Submit)
        return;

    set_status("vault.busy.creating");
    const std::string id = WalletStore::mint_id(ev.name);
    auto job = std::make_shared<Job>();
    job->next = Mode::Locked;
    job->wallet = id;
    m_job = job;
    std::thread([job, pass = std::move(ev.pass), name = std::move(ev.name),
                    path = m_store.vault_path(id), store = &m_store,
                    id]() mutable {
        try {
            vault::Wallet wallet;
            wallet.entropy = SecureBytes(16);
            randombytes_buf(wallet.entropy.data(), wallet.entropy.size());
            vault::save(path, pass, wallet, vault::kdf_sensitive());
            store->write_meta(id, { name, 1, 0, 0, 0, kKindHd, {} });
            job->secret = crypto::entropy_to_mnemonic(wallet.entropy);
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

void VaultPage::start_import(ImportView::Event ev)
{
    if (ev.err) {
        set_status(ev.err);
        return;
    }
    if (ev.type == ImportView::Event::Type::Back) {
        enter(m_store.empty() ? Mode::NoWallets : Mode::Locked);
        return;
    }
    if (ev.type != ImportView::Event::Type::Submit)
        return;

    // A watch import is pure filesystem — no vault, no proving, no
    // job; the wallet exists the moment its sidecar does.
    if (!ev.watch.empty()) {
        const std::string id
            = m_store.create_watch(ev.name, ev.watch, ev.watch_family);
        switch_active(id);
        return;
    }
    if (!ev.wallet)
        return;

    set_status("vault.busy.creating");
    const std::string id = WalletStore::mint_id(ev.name);
    const char* kind = !ev.wallet->entropy.empty() ? kKindHd
        : ev.wallet->imported.front().label == keyd::kEd25519KeyLabel
        ? kKindEd25519
        : kKindSecp;
    auto job = std::make_shared<Job>();
    job->next = Mode::Locked;
    job->wallet = id;
    m_job = job;
    std::thread([job, pass = std::move(ev.pass), name = std::move(ev.name),
                    wallet = std::move(*ev.wallet), preset = ev.preset, kind,
                    path = m_store.vault_path(id), store = &m_store,
                    id]() mutable {
        try {
            prove_wallet(wallet, keyd::DerivePreset(preset));
            vault::save(path, pass, wallet, vault::kdf_sensitive());
            store->write_meta(id, { name, 1, 0, preset, 0, kind, {} });
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

void VaultPage::start_unlock(SecureBytes pass)
{
    std::string spawn_err;
    if (!m_session.ensure(m_vault_path, &spawn_err)) {
        m_status = spawn_err;
        m_status_is_key = false;
        return;
    }
    set_status("vault.busy.unlocking");
    auto job = std::make_shared<Job>();
    job->next = Mode::Unlocked;
    m_job = job;
    // The client is untouched by the frame loop while a job is in
    // flight (buttons disabled), so handing the pointer to the worker
    // is single-owner in practice.
    keyd::KeydClient* keyd = m_session.client();
    std::thread([job, keyd, pass = std::move(pass)]() mutable {
        if (keyd->unlock(pass)) {
            job->phase.store(1);
        } else {
            job->error = keyd->last_error();
            job->phase.store(2);
        }
    }).detach();
}

void VaultPage::start_backup(SecureBytes pass)
{
    std::string spawn_err;
    if (!m_session.ensure(m_vault_path, &spawn_err)) {
        m_status = spawn_err;
        m_status_is_key = false;
        return;
    }
    m_status.clear();
    auto job = std::make_shared<Job>();
    job->next = Mode::Unlocked;
    m_job = job;
    keyd::KeydClient* keyd = m_session.client();
    std::thread([job, keyd, pass = std::move(pass)]() mutable {
        auto revealed = keyd->reveal(pass);
        if (!revealed) {
            job->error = keyd->last_error();
            job->phase.store(2);
            return;
        }
        try {
            job->secret_kind = revealed->kind;
            if (revealed->kind == keyd::RevealKind::SeedEntropy) {
                job->secret = crypto::entropy_to_mnemonic(revealed->secret);
            } else if (revealed->kind == keyd::RevealKind::Ed25519Key
                && revealed->secret.size() == 32) {
                // Solana key wallet: back up in the same base58
                // keypair encoding it was imported from, so it pastes
                // straight back into Phantom.
                job->secret = crypto::sol_key_to_base58(
                    std::span<const uint8_t, 32>(revealed->secret.data(), 32));
            } else {
                // secp256k1 key wallet: the backup is the key itself,
                // shown as hex in guarded memory.
                SecureBytes hex(2 + revealed->secret.size() * 2 + 1);
                std::memcpy(hex.data(), "0x", 2);
                sodium_bin2hex(reinterpret_cast<char*>(hex.data()) + 2,
                    hex.size() - 2, revealed->secret.data(),
                    revealed->secret.size());
                job->secret = std::move(hex);
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

void VaultPage::handle_accounts(AccountsView::Event ev)
{
    switch (ev.type) {
    case AccountsView::Event::Type::Select:
        m_meta.active = ev.index;
        m_store.write_meta(m_active, m_meta);
        break;
    case AccountsView::Event::Type::Add:
        ++m_meta.count;
        m_store.write_meta(m_active, m_meta);
        m_store.rescan(); // the card face shows the count
        m_session.push_address(m_meta.count - 1, m_meta.preset);
        for (const std::string& fam : wallet_families(m_meta))
            if (fam != family_key(active_family()))
                m_session.refresh_family(
                    fam, m_meta.count, uint8_t(family_preset(m_meta, fam)));
        // Only the newborn row needs a number; the rest are current.
        fetch_balances(int(m_meta.count) - 1);
        break;
    case AccountsView::Event::Type::LabelEdit:
        if (ev.index >= m_meta.labels.size())
            m_meta.labels.resize(ev.index + 1);
        m_meta.labels[ev.index] = std::move(ev.label);
        m_store.write_meta(m_active, m_meta);
        break;
    case AccountsView::Event::Type::BtcFormat: {
        // One key, another costume: a BTC-born wallet re-dresses its
        // main line, everyone else flips the sidecar override. The
        // read-only pages follow by themselves — their follow key is
        // the address, and the address just changed.
        const uint8_t chosen = uint8_t(ev.index);
        if (family_key(keyd::preset_family(keyd::DerivePreset(m_meta.preset)))
            == std::string("btc")) {
            m_meta.preset = chosen;
            m_session.refresh_addresses(m_meta.count, m_meta.preset);
            m_bal_stale = true; // settled when the dialog closes
        } else {
            m_meta.btc_preset = chosen;
        }
        m_store.write_meta(m_active, m_meta);
        m_session.refresh_family(
            "btc", m_meta.count, uint8_t(family_preset(m_meta, "btc")));
        break;
    }
    case AccountsView::Event::Type::RefreshBalance:
        fetch_balances(int(ev.index));
        break;
    case AccountsView::Event::Type::Lock:
        if (m_session.client() && m_session.client()->lock()) {
            m_session.mark_unlocked(false);
            enter(Mode::Locked);
        }
        break;
    case AccountsView::Event::Type::Backup:
        start_backup(std::move(ev.pass));
        break;
    case AccountsView::Event::Type::None:
        if (ev.err)
            set_status(ev.err);
        break;
    }
}

}
