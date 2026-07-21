// The rig's construction contract: whatever a shell mounts it on, the
// vault always comes up, and a broken pane is a report on that pane —
// never a dead wallet, never a silent blank.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <filesystem>
#include <string_view>

#include "keyd/child.hpp"
#include "ui/rig/wallet_rig.hpp"

int main(int argc, char** argv)
{
    // The keyd spawn path re-executes the hosting binary as the
    // trust-plane child; that mode must never enter the test runner.
    if (argc > 1 && std::string_view(argv[1]) == "--keyd-child")
        return izan::keyd::child_main(argc, argv);
    return doctest::Context(argc, argv).run();
}

using izan::ui::WalletRig;

namespace {

std::filesystem::path fresh_dir(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}

TEST_CASE("a rig on the shipped config raises every pane")
{
    const auto state = fresh_dir("izan-rig-state");
    const auto wallets = fresh_dir("izan-rig-wallets");
    WalletRig rig({ std::filesystem::path(IZAN_SOURCE_DIR) / "data", state,
                      wallets, "izan.exe" },
        "");
    CHECK_FALSE(rig.unlocked());
    CHECK(rig.portfolio_error().empty());
    CHECK(rig.send_error().empty());
    CHECK(rig.swap_error().empty());
    CHECK(rig.history_error().empty());
}

TEST_CASE("a missing config takes down panes, never the vault")
{
    const auto state = fresh_dir("izan-rig-state2");
    const auto wallets = fresh_dir("izan-rig-wallets2");
    WalletRig rig({ std::filesystem::path(IZAN_SOURCE_DIR) / "no-such-data",
                      state, wallets, "izan.exe" },
        "ghost");
    // The vault is alive and remembers what it was told to activate;
    // the config-fed panes each explain themselves.
    CHECK_FALSE(rig.unlocked());
    CHECK_FALSE(rig.portfolio_error().empty());
    CHECK_FALSE(rig.send_error().empty());
    CHECK_FALSE(rig.swap_error().empty());
    CHECK_FALSE(rig.history_error().empty());
}
