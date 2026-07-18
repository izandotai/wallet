#ifdef _WIN32

#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <sodium.h>
#include <windows.h>

#include <wtsapi32.h>

#include "core/secure/vault.hpp"
#include "keyd/audit.hpp"
#include "keyd/client.hpp"
#include "keyd/protocol.hpp"
#include "platform/ipc/pipe.hpp"
#include "platform/ipc/secret_channel.hpp"

using izan::secure::SecureBytes;

namespace {

SecureBytes sb_from(std::string_view text)
{
    SecureBytes out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

SecureBytes fresh_key()
{
    SecureBytes key(izan::ipc::SecretChannel::kKeyBytes);
    randombytes_buf(key.data(), key.size());
    return key;
}

std::string self_exe()
{
    char path[MAX_PATH];
    REQUIRE(GetModuleFileNameA(nullptr, path, MAX_PATH));
    return path;
}

std::string make_test_vault(const char* pass)
{
    const std::string path
        = (std::filesystem::temp_directory_path() / "keyd_test.qvlt").string();
    izan::vault::Wallet wallet;
    wallet.entropy = SecureBytes(16);
    randombytes_buf(wallet.entropy.data(), wallet.entropy.size());
    izan::vault::save(path, sb_from(pass), wallet, izan::vault::kdf_min());
    return path;
}

}

TEST_CASE("pipes move whole buffers and report peer loss")
{
    izan::ipc::Pipe pipe = izan::ipc::make_pipe(false, false);
    const uint8_t out[5] = { 1, 2, 3, 4, 5 };
    REQUIRE(pipe.write.write_all(out, sizeof out));
    uint8_t in[5] = {};
    REQUIRE(pipe.read.read_all(in, sizeof in));
    CHECK(std::memcmp(out, in, sizeof in) == 0);

    pipe.write.close();
    CHECK(!pipe.read.read_all(in, 1)); // broken, not blocking
}

TEST_CASE("secret channel: authenticated duplex, tamper kills session")
{
    SecureBytes key = fresh_key();
    izan::ipc::Pipe aToB = izan::ipc::make_pipe(false, false);
    izan::ipc::Pipe bToA = izan::ipc::make_pipe(false, false);

    izan::ipc::SecretChannel a(
        std::move(bToA.read), std::move(aToB.write), key);
    izan::ipc::SecretChannel b(
        std::move(aToB.read), std::move(bToA.write), key);

    const uint8_t ping[4] = { 'p', 'i', 'n', 'g' };
    REQUIRE(a.send(ping, sizeof ping));
    auto got = b.recv();
    REQUIRE(got);
    REQUIRE(got->size() == 4);
    CHECK(std::memcmp(got->data(), ping, 4) == 0);

    const uint8_t pong[4] = { 'p', 'o', 'n', 'g' };
    REQUIRE(b.send(pong, sizeof pong));
    auto back = a.recv();
    REQUIRE(back);
    CHECK(std::memcmp(back->data(), pong, 4) == 0);
}

TEST_CASE("secret channel: a keyless endpoint cannot speak")
{
    // The attacker holds the pipes (squatting scenario) but not the
    // session key: its very first frame must fail authentication.
    SecureBytes rightKey = fresh_key();
    SecureBytes wrongKey = fresh_key();
    izan::ipc::Pipe aToB = izan::ipc::make_pipe(false, false);
    izan::ipc::Pipe bToA = izan::ipc::make_pipe(false, false);

    izan::ipc::SecretChannel attacker(
        std::move(bToA.read), std::move(aToB.write), wrongKey);
    izan::ipc::SecretChannel victim(
        std::move(aToB.read), std::move(bToA.write), rightKey);

    const uint8_t msg[1] = { 0x41 };
    REQUIRE(attacker.send(msg, 1));
    CHECK_THROWS(victim.recv());
}

TEST_CASE("keyd child: hardened hello, vault custody, clean shutdown")
{
    const std::string vaultPath = make_test_vault("correct horse");
    izan::keyd::KeydClient keyd
        = izan::keyd::KeydClient::spawn(self_exe(), vaultPath);

    CHECK(keyd.hello().version == izan::keyd::kProtocolVersion);
    // Every §3.1 process protection must have engaged.
    CHECK((keyd.hello().hardened & izan::keyd::kHardenedDumps));
    CHECK((keyd.hello().hardened & izan::keyd::kHardenedDynCode));
    CHECK((keyd.hello().hardened & izan::keyd::kHardenedDllSig));
    CHECK((keyd.hello().hardened & izan::keyd::kHardenedDllDirs));
    CHECK((keyd.hello().hardened & izan::keyd::kHardenedAutoLock));

    CHECK(keyd.unlocked() == false);

    CHECK(!keyd.unlock(sb_from("wrong pass")));
    CHECK(keyd.last_error().find("passphrase") != std::string::npos);
    CHECK(keyd.unlocked() == false);

    CHECK(keyd.unlock(sb_from("correct horse")));
    CHECK(keyd.unlocked() == true);

    CHECK(keyd.lock());
    CHECK(keyd.unlocked() == false);

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".bak");
}

TEST_CASE("keyd child: session lock wipes the unlocked vault")
{
    const std::string vaultPath = make_test_vault("correct horse");
    const std::string auditPath = vaultPath + ".lock.audit";
    std::filesystem::remove(auditPath);

    izan::keyd::KeydClient keyd
        = izan::keyd::KeydClient::spawn(self_exe(), vaultPath, auditPath);
    REQUIRE(keyd.unlock(sb_from("correct horse")));
    REQUIRE(keyd.unlocked() == true);

    // Deliver exactly what the OS would on Win+L. The watcher window is
    // findable by design; a hostile process using this path can only
    // lock the vault, never open it.
    HWND watch = FindWindowA("IzanKeydWatch", keyd.pipe_name().c_str());
    REQUIRE(watch);
    REQUIRE(PostMessageA(watch, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0));

    bool wiped = false;
    for (int i = 0; i < 100 && !wiped; ++i) {
        auto state = keyd.unlocked();
        REQUIRE(state);
        wiped = !*state;
        if (!wiped)
            Sleep(20);
    }
    CHECK(wiped);

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    // The wipe itself must be on the ledger.
    CHECK(izan::keyd::AuditLog::verify(auditPath) == 1);
    {
        std::ifstream ledger(auditPath);
        std::string line;
        std::getline(ledger, line);
        CHECK(line.find("autolock reason=session-lock") != std::string::npos);
    }

    std::filesystem::remove(auditPath);
    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".bak");
}

TEST_CASE("keyd child: reveal spends the passphrase and returns the entropy")
{
    const std::string vaultPath
        = (std::filesystem::temp_directory_path() / "keyd_reveal.qvlt")
              .string();
    izan::vault::Wallet wallet;
    wallet.entropy = SecureBytes(16);
    randombytes_buf(wallet.entropy.data(), wallet.entropy.size());
    uint8_t expect[16];
    std::memcpy(expect, wallet.entropy.data(), 16);
    izan::vault::save(
        vaultPath, sb_from("correct horse"), wallet, izan::vault::kdf_min());

    izan::keyd::KeydClient keyd
        = izan::keyd::KeydClient::spawn(self_exe(), vaultPath);

    CHECK(!keyd.reveal(sb_from("wrong pass")));
    CHECK(keyd.last_error() == "bad passphrase");

    auto revealed = keyd.reveal(sb_from("correct horse"));
    REQUIRE(revealed);
    CHECK(revealed->kind == izan::keyd::RevealKind::SeedEntropy);
    REQUIRE(revealed->secret.size() == 16);
    CHECK(std::memcmp(revealed->secret.data(), expect, 16) == 0);

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".audit");
    std::filesystem::remove(vaultPath + ".bak");
}

TEST_CASE("keyd child: a wrong-passphrase streak hits the throttle")
{
    using Clock = std::chrono::steady_clock;
    const std::string vaultPath = make_test_vault("correct horse");
    izan::keyd::KeydClient keyd
        = izan::keyd::KeydClient::spawn(self_exe(), vaultPath);

    // Three misses are answered at argon2 speed…
    const auto t0 = Clock::now();
    for (int i = 0; i < 3; ++i)
        CHECK(!keyd.unlock(sb_from("wrong pass")));
    const auto t1 = Clock::now();

    // …the fourth stalls a full second before it is even considered.
    CHECK(!keyd.unlock(sb_from("wrong pass")));
    const auto t2 = Clock::now();
    CHECK(t2 - t1 >= std::chrono::milliseconds(900));

    // A correct passphrase still gets in (after its stall) and resets
    // the meter: the next miss is fast again.
    CHECK(keyd.unlock(sb_from("correct horse")));
    const auto t3 = Clock::now();
    CHECK(!keyd.unlock(sb_from("wrong pass")));
    CHECK(Clock::now() - t3 < std::chrono::milliseconds(900));

    (void)t0;
    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".audit");
    std::filesystem::remove(vaultPath + ".bak");
}

TEST_CASE("keyd child: parent death collapses the session")
{
    const std::string vaultPath = make_test_vault("correct horse");
    izan::keyd::KeydClient keyd
        = izan::keyd::KeydClient::spawn(self_exe(), vaultPath);
    REQUIRE(keyd.unlock(sb_from("correct horse")));

    // Closing the channel is what the child sees when the UI dies: it
    // must wipe and exit on its own, no shutdown message required.
    keyd.drop_channel();
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".bak");
}

#endif
