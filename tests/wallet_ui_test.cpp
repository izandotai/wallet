// The wallet UI's headless layer: the directory store, the import
// model and the envelope packer — everything a page draws is decided
// here, where a test can reach it without a window.

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include <filesystem>
#include <fstream>

#include "keyd/signer.hpp"
#include "ui/wallet/import_model.hpp"
#include "ui/wallet/presets.hpp"
#include "ui/wallet/store.hpp"

using namespace izan;
using keyd::DerivePreset;

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir()
        : path(std::filesystem::temp_directory_path()
              / ("izan-store-" + std::to_string(::rand())))
    {
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void touch_vault(const ui::WalletStore& store, const std::string& id)
{
    std::ofstream f(store.vault_path(id), std::ios::binary);
    f << "x";
}

}

TEST_CASE("the store lists wallets and round-trips their sidecars")
{
    TempDir tmp;
    ui::WalletStore store(tmp.path);
    CHECK(store.empty());
    CHECK(store.first_id().empty());

    const std::string id = ui::WalletStore::mint_id("老干妈");
    CHECK(id.size() == 16);
    // Same name, fresh salt: ids must not collide.
    CHECK(id != ui::WalletStore::mint_id("老干妈"));

    touch_vault(store, id);
    store.write_meta(id, { "老干妈", 3, 2, 5 });
    store.rescan();
    REQUIRE(store.wallets().size() == 1);
    CHECK(store.wallets().front().id == id);
    CHECK(store.wallets().front().name == "老干妈");
    CHECK(store.known(id));
    CHECK(store.first_id() == id);

    const ui::AccountsMeta meta = store.read_meta(id);
    CHECK(meta.count == 3);
    CHECK(meta.active == 2);
    CHECK(meta.preset == 5);

    // Kind badge and per-account notes ride the same sidecar.
    store.write_meta(
        id, { "老干妈", 3, 2, 5, 0, ui::kKindHd, { "冷钱包", "" } });
    const ui::AccountsMeta noted = store.read_meta(id);
    CHECK(noted.kind == ui::kKindHd);
    REQUIRE(noted.labels.size() == 2);
    CHECK(noted.labels[0] == "冷钱包");
}

TEST_CASE("a pinned wallet floats to the top and stays pinned on disk")
{
    TempDir tmp;
    ui::WalletStore store(tmp.path);
    const std::string first = ui::WalletStore::mint_id("元老");
    const std::string second = ui::WalletStore::mint_id("新人");
    touch_vault(store, first);
    store.write_meta(first, { "元老" });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    touch_vault(store, second);
    store.write_meta(second, { "新人" });
    store.rescan();
    REQUIRE(store.wallets().size() == 2);
    // Birth order first: the elder on top.
    CHECK(store.wallets().front().id == first);
    CHECK(!store.wallets().front().pinned);

    // Pin the younger one; it leapfrogs.
    ui::AccountsMeta meta = store.read_meta(second);
    meta.pinned = true;
    store.write_meta(second, meta);
    store.rescan();
    CHECK(store.wallets().front().id == second);
    CHECK(store.wallets().front().pinned);
    // The flag survives its own file.
    CHECK(store.read_meta(second).pinned);
    // Unpin: the old order returns.
    meta.pinned = false;
    store.write_meta(second, meta);
    store.rescan();
    CHECK(store.wallets().front().id == first);
}

TEST_CASE("deleting a wallet leaves nothing on disk")
{
    TempDir tmp;
    ui::WalletStore store(tmp.path);
    const std::string id = ui::WalletStore::mint_id("doomed");
    touch_vault(store, id);
    store.write_meta(id, { "doomed", 1, 0, 0 });
    // A stale audit ledger goes with it.
    {
        std::ofstream a(store.vault_path(id) + ".audit");
        a << "x";
    }
    store.rescan();
    REQUIRE(store.known(id));

    store.delete_wallet(id);
    CHECK(!store.known(id));
    CHECK(!std::filesystem::exists(store.vault_path(id)));
    CHECK(!std::filesystem::exists(store.meta_path(id)));
    CHECK(!std::filesystem::exists(store.vault_path(id) + ".audit"));
}

TEST_CASE("a hand-edited sidecar cannot smuggle bad indices in")
{
    TempDir tmp;
    ui::WalletStore store(tmp.path);
    const std::string id = ui::WalletStore::mint_id("w");
    touch_vault(store, id);
    store.write_meta(id, { "w", 2, 7, keyd::kDerivePresetCount });

    const ui::AccountsMeta meta = store.read_meta(id);
    CHECK(meta.active == 0); // out of range → first account
    CHECK(meta.preset == 0); // unknown preset → default

    // A wallet with no sidecar displays its id and sane defaults.
    const std::string bare = ui::WalletStore::mint_id("bare");
    touch_vault(store, bare);
    store.rescan();
    const ui::AccountsMeta none = store.read_meta(bare);
    CHECK(none.name.empty());
    CHECK(none.count == 1);
}

TEST_CASE("the store refuses names the wallet list cannot hold")
{
    TempDir tmp;
    ui::WalletStore store(tmp.path);
    CHECK(store.valid_new_name("任意 Unicode 名 ★"));
    CHECK(!store.valid_new_name(""));
    CHECK(!store.valid_new_name(std::string(49, 'x')));
    CHECK(!store.valid_new_name("control\x01char"));

    const std::string id = ui::WalletStore::mint_id("taken");
    touch_vault(store, id);
    store.write_meta(id, { "taken", 1, 0, 0 });
    store.rescan();
    CHECK(!store.valid_new_name("taken"));
}

TEST_CASE("the import model walks paste, preview and selection")
{
    ui::ImportModel model;
    CHECK(!model.recognized());
    CHECK(model.offered().empty());

    // The zero-entropy mnemonic: every family previews its anchor.
    model.update("abandon abandon abandon abandon abandon abandon "
                 "abandon abandon abandon abandon abandon about");
    CHECK(model.kind() == crypto::SecretKind::Mnemonic);
    CHECK(model.offered().size() == keyd::kDerivePresetCount);
    CHECK(model.selected() == uint8_t(DerivePreset::MetaMask));
    CHECK(model.preview(DerivePreset::BtcSegwit)
        == "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu");
    CHECK(model.preview(DerivePreset::SolPhantom)
        == "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk");

    model.select(DerivePreset::SolPhantom);
    CHECK(model.selected() == uint8_t(DerivePreset::SolPhantom));

    // A WIF flips the offer to BTC formats and lands on native segwit;
    // the stale Solana selection cannot survive the change.
    model.update("5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ");
    CHECK(model.kind() == crypto::SecretKind::Wif);
    CHECK(model.offered().size() == 4);
    CHECK(model.selected() == uint8_t(DerivePreset::BtcSegwit));
    CHECK(model.preview(DerivePreset::BtcLegacy)
        == "1LoVGDgRs9hTfTNJNuXKSpywcbdvwRXpmK");
    model.select(DerivePreset::MetaMask); // not offered for a WIF
    CHECK(model.selected() == uint8_t(DerivePreset::BtcSegwit));

    // A Solana keypair offers exactly one home.
    model.update("27npWoNE4HfmLeQo1TyWcW7NEA28qnsnDK7kcttDQEWr"
                 "CWnro83HMJ97rMmpvYYZRwDAvG4KRuB7hTBacvwD7bgi");
    CHECK(model.kind() == crypto::SecretKind::SolKey);
    REQUIRE(model.offered().size() == 1);
    CHECK(model.selected() == uint8_t(DerivePreset::SolPhantom));
    CHECK(model.preview(DerivePreset::SolPhantom)
        == "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk");
    const auto solWallet
        = model.build("27npWoNE4HfmLeQo1TyWcW7NEA28qnsnDK7kcttDQEWr"
                      "CWnro83HMJ97rMmpvYYZRwDAvG4KRuB7hTBacvwD7bgi");
    REQUIRE(solWallet);
    REQUIRE(solWallet->imported.size() == 1);
    CHECK(solWallet->imported.front().label == keyd::kEd25519KeyLabel);
    CHECK_NOTHROW(ui::prove_wallet(*solWallet, DerivePreset::SolPhantom));

    // Garbage clears the offer; build refuses.
    model.update("not a secret");
    CHECK(!model.recognized());
    CHECK(!model.build("not a secret"));

    // A recognized text builds the wallet its kind dictates.
    const auto wallet
        = model.build("5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ");
    REQUIRE(wallet);
    CHECK(wallet->entropy.empty());
    REQUIRE(wallet->imported.size() == 1);
    CHECK_NOTHROW(ui::prove_wallet(*wallet, DerivePreset::BtcSegwit));
}

TEST_CASE("make_envelope and parse_proposal are exact inverses")
{
    const std::vector<uint8_t> tx { 0x02, 0xAB, 0xCD };
    const std::vector<uint8_t> env
        = keyd::make_envelope(DerivePreset::BtcTaproot, 0x01020304, tx);
    const keyd::ProposalBody body = keyd::parse_proposal(env);
    CHECK(body.preset == DerivePreset::BtcTaproot);
    CHECK(body.account == 0x01020304);
    REQUIRE(body.tx.size() == tx.size());
    CHECK(std::equal(tx.begin(), tx.end(), body.tx.begin()));
}

TEST_CASE("a watch wallet is a sidecar with no vault behind it")
{
    TempDir tmp;
    ui::WalletStore store(tmp.path);
    constexpr const char* kAddr = "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045";

    const std::string id = store.create_watch("盯着老鲸", kAddr, "evm");
    REQUIRE(store.known(id));
    // No vault file was ever written — the wallet is its sidecar.
    CHECK_FALSE(std::filesystem::exists(store.vault_path(id)));

    const auto& listed = store.wallets();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].kind == ui::kKindWatch);
    CHECK(listed[0].name == "盯着老鲸");
    CHECK(listed[0].count == 1);

    const ui::AccountsMeta meta = store.read_meta(id);
    REQUIRE(meta.watch.size() == 1);
    CHECK(meta.watch[0] == kAddr);
    CHECK(meta.kind == ui::kKindWatch);

    // Deleting must not trip over the vault that never existed.
    store.delete_wallet(id);
    CHECK_FALSE(store.known(id));
    CHECK_FALSE(std::filesystem::exists(store.meta_path(id)));
}

TEST_CASE("the import model recognizes a watch address and builds no vault")
{
    ui::ImportModel model;
    // Any-case address normalizes to its EIP-55 form.
    model.update("0xd8da6bf26964af9d7eed9e03e53415d37aa96045");
    CHECK(model.kind() == crypto::SecretKind::EthAddress);
    CHECK(model.recognized());
    CHECK(model.offered().empty());
    CHECK(
        model.watch_address() == "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045");
    // A watch address describes no spendable wallet.
    CHECK_FALSE(
        model.build("0xd8da6bf26964af9d7eed9e03e53415d37aa96045").has_value());
    // Garbage that merely looks addressish is still refused.
    model.update("0xd8da6bf26964af9d7eed9e03e53415d37aa9604");
    CHECK_FALSE(model.recognized());
}

TEST_CASE("watch imports speak three families and remember which")
{
    ui::ImportModel model;
    // BTC (native segwit) and Solana addresses become watch wallets
    // of their own family; normalization leaves base58 untouched.
    model.update("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu");
    CHECK(model.kind() == crypto::SecretKind::BtcAddress);
    CHECK(
        model.watch_address() == "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu");
    CHECK(crypto::watch_family(model.kind()) == std::string("btc"));

    model.update("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
    CHECK(model.kind() == crypto::SecretKind::SolAddress);
    CHECK(crypto::watch_family(model.kind()) == std::string("sol"));
    CHECK_FALSE(model.build("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v")
            .has_value());

    // The family survives the sidecar round trip and a bad hand-edit
    // falls back to evm.
    TempDir tmp;
    ui::WalletStore store(tmp.path);
    const std::string id = store.create_watch(
        "鲸鱼SOL", "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v", "sol");
    CHECK(store.read_meta(id).watch_family == "sol");
    ui::AccountsMeta meta = store.read_meta(id);
    meta.watch_family = "cosmos";
    store.write_meta(id, meta);
    CHECK(store.read_meta(id).watch_family == "evm");
}

TEST_CASE("an HD wallet answers for every family, keys stay on their curve")
{
    using izan::keyd::DerivePreset;
    izan::ui::AccountsMeta hd;
    hd.kind = izan::ui::kKindHd;
    hd.preset = uint8_t(DerivePreset::LedgerLive);
    CHECK(izan::ui::wallet_families(hd)
        == std::vector<std::string> { "evm", "btc", "sol" });
    // The birth preset answers for its own family; the others get the
    // industry defaults, never a second guess.
    CHECK(izan::ui::family_preset(hd, "evm") == DerivePreset::LedgerLive);
    CHECK(izan::ui::family_preset(hd, "btc") == DerivePreset::BtcSegwit);
    CHECK(izan::ui::family_preset(hd, "sol") == DerivePreset::SolPhantom);

    // A BTC-born seed keeps its chosen address format for BTC.
    izan::ui::AccountsMeta btc_born;
    btc_born.kind = izan::ui::kKindHd;
    btc_born.preset = uint8_t(DerivePreset::BtcNestedSegwit);
    CHECK(izan::ui::family_preset(btc_born, "btc")
        == DerivePreset::BtcNestedSegwit);
    CHECK(izan::ui::family_preset(btc_born, "evm") == DerivePreset::MetaMask);

    // Pre-manager sidecars never recorded a kind — those were all HD.
    izan::ui::AccountsMeta legacy;
    CHECK(izan::ui::wallet_families(legacy).size() == 3);

    izan::ui::AccountsMeta key;
    key.kind = izan::ui::kKindSecp;
    key.preset = uint8_t(DerivePreset::BtcSegwit);
    CHECK(izan::ui::wallet_families(key) == std::vector<std::string> { "btc" });

    izan::ui::AccountsMeta sol_key;
    sol_key.kind = izan::ui::kKindEd25519;
    sol_key.preset = uint8_t(DerivePreset::SolPhantom);
    CHECK(izan::ui::wallet_families(sol_key)
        == std::vector<std::string> { "sol" });

    izan::ui::AccountsMeta watch;
    watch.kind = izan::ui::kKindWatch;
    watch.watch_family = "btc";
    CHECK(
        izan::ui::wallet_families(watch) == std::vector<std::string> { "btc" });
    izan::ui::AccountsMeta watch_legacy;
    watch_legacy.kind = izan::ui::kKindWatch;
    CHECK(izan::ui::wallet_families(watch_legacy)
        == std::vector<std::string> { "evm" });
}

TEST_CASE("the chosen BTC costume outranks the family default")
{
    using izan::keyd::DerivePreset;
    izan::ui::AccountsMeta hd;
    hd.kind = izan::ui::kKindHd;
    hd.preset = uint8_t(DerivePreset::MetaMask);
    CHECK(izan::ui::family_preset(hd, "btc") == DerivePreset::BtcSegwit);
    hd.btc_preset = uint8_t(DerivePreset::BtcLegacy);
    CHECK(izan::ui::family_preset(hd, "btc") == DerivePreset::BtcLegacy);
    // The override speaks only for BTC.
    CHECK(izan::ui::family_preset(hd, "evm") == DerivePreset::MetaMask);
    CHECK(izan::ui::family_preset(hd, "sol") == DerivePreset::SolPhantom);

    // A hand-edited sidecar cannot smuggle a non-BTC preset through
    // the override slot.
    const std::string dir
        = (std::filesystem::temp_directory_path() / "izan_btcfmt_test")
              .string();
    std::filesystem::remove_all(dir);
    izan::ui::WalletStore store { dir };
    izan::ui::AccountsMeta meta;
    meta.name = "w";
    meta.kind = izan::ui::kKindHd;
    meta.btc_preset = uint8_t(DerivePreset::SolPhantom); // not BTC
    store.write_meta("aaaa", meta);
    CHECK(store.read_meta("aaaa").btc_preset == 0);
    meta.btc_preset = uint8_t(DerivePreset::BtcTaproot);
    store.write_meta("aaaa", meta);
    CHECK(store.read_meta("aaaa").btc_preset
        == uint8_t(DerivePreset::BtcTaproot));
    std::filesystem::remove_all(dir);
}
