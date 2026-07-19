// The wallet UI's headless layer: the directory store, the import
// model and the envelope packer — everything a page draws is decided
// here, where a test can reach it without a window.

#include <doctest/doctest.h>

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
    store.write_meta(id, { "老干妈", 3, 2, 5, ui::kKindHd, { "冷钱包", "" } });
    const ui::AccountsMeta noted = store.read_meta(id);
    CHECK(noted.kind == ui::kKindHd);
    REQUIRE(noted.labels.size() == 2);
    CHECK(noted.labels[0] == "冷钱包");
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

    const std::string id = store.create_watch("盯着老鲸", kAddr);
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
