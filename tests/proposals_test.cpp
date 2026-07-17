#ifdef _WIN32

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sodium.h>
#include <windows.h>

extern "C" {
#include <ecdsa.h>
#include <secp256k1.h>
#include <sha3.h>
}

#include "core/crypto/bip39.hpp"
#include "core/crypto/eth.hpp"
#include "core/secure/vault.hpp"
#include "keyd/audit.hpp"
#include "keyd/client.hpp"
#include "keyd/proposals.hpp"
#include "keyd/protocol.hpp"
#include "keyd/signer.hpp"
#include "platform/ipc/named_pipe.hpp"

using izan::secure::SecureBytes;
using namespace izan::keyd;

namespace {

// Hardhat/Anvil development mnemonic; its account #0 is the most
// widely cross-checked address in the EVM ecosystem — an external
// anchor for "the signature came from the vault's seed".
const char* kDevMnemonic
    = "test test test test test test test test test test test junk";
const char* kDevAccount0 = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266";

SecureBytes sb_from(std::string_view text)
{
    SecureBytes out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::string recovered_signer(
    const ApprovedSignature& sig, const uint8_t digest[32])
{
    uint8_t rs[64];
    std::memcpy(rs, sig.r.data(), 32);
    std::memcpy(rs + 32, sig.s.data(), 32);
    uint8_t pub[65];
    REQUIRE(
        ecdsa_recover_pub_from_sig(&secp256k1, pub, rs, digest, sig.y_parity)
        == 0);
    return izan::crypto::eth_address(std::span<const uint8_t, 65>(pub, 65));
}

std::string self_exe()
{
    char path[MAX_PATH];
    REQUIRE(GetModuleFileNameA(nullptr, path, MAX_PATH));
    return path;
}

std::string temp_file(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

std::string make_test_vault(const char* pass)
{
    const std::string path = temp_file("proposals_test.qvlt");
    izan::vault::Wallet wallet;
    wallet.entropy = izan::crypto::mnemonic_to_entropy(kDevMnemonic);
    izan::vault::save(path, sb_from(pass), wallet, izan::vault::kdf_min());
    return path;
}

// A stranger process at the proposal pipe: no session key, just the
// public wire format.
std::optional<std::vector<uint8_t>> raw_transact(
    const std::string& pipe, const std::vector<uint8_t>& frame)
{
    izan::ipc::NamedPipeClient client(pipe);
    return client.transact(frame.data(), frame.size());
}

std::vector<uint8_t> submit_frame(
    uint8_t flag, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> frame { uint8_t(PipeOp::Submit), flag };
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

ProposalState query_state(const std::string& pipe, uint64_t id)
{
    std::vector<uint8_t> frame(9);
    frame[0] = uint8_t(PipeOp::Query);
    put_u64le(frame.data() + 1, id);
    auto reply = raw_transact(pipe, frame);
    REQUIRE(reply);
    REQUIRE(reply->size() == 2);
    REQUIRE((*reply)[0] == uint8_t(PipeOp::QueryState));
    return ProposalState((*reply)[1]);
}

}

TEST_CASE("named pipe: a squatter on the name makes the server fail loudly")
{
    izan::ipc::NamedPipeServer squatter("izan.test.squat");
    CHECK_THROWS(izan::ipc::NamedPipeServer("izan.test.squat"));
}

TEST_CASE("named pipe: message round trip and client turnover")
{
    izan::ipc::NamedPipeServer server("izan.test.echo");
    std::thread echo([&] {
        for (int client = 0; client < 2; ++client) {
            REQUIRE(server.wait_client());
            for (;;) {
                auto msg = server.read_message();
                if (!msg)
                    break;
                REQUIRE(server.write_message(msg->data(), msg->size()));
            }
            server.drop_client();
        }
    });

    for (int round = 0; round < 2; ++round) {
        izan::ipc::NamedPipeClient client("izan.test.echo");
        const uint8_t ping[3] = { 7, 8, 9 };
        auto reply = client.transact(ping, sizeof ping);
        REQUIRE(reply);
        REQUIRE(reply->size() == 3);
        CHECK(std::memcmp(reply->data(), ping, 3) == 0);
    }
    echo.join();
}

TEST_CASE("proposal queue: pending until resolved exactly once")
{
    ProposalQueue queue;
    const uint64_t id = queue.submit({ 1, 2, 3 }, Provenance::Anonymous);
    CHECK(queue.state(id) == ProposalState::Pending);
    CHECK(queue.state(id + 999) == ProposalState::Unknown);
    CHECK(queue.pending_ids().size() == 1);

    CHECK(queue.resolve(id, ProposalState::Approved));
    CHECK(queue.state(id) == ProposalState::Approved);
    CHECK(!queue.resolve(id, ProposalState::Denied)); // verdicts are final
    CHECK(queue.pending_ids().empty());
}

TEST_CASE("audit ledger: chain verifies, tampering breaks it")
{
    const std::string path = temp_file("proposals_test.audit");
    std::filesystem::remove(path);

    CHECK(AuditLog::verify(path) == 0); // no ledger yet is not a break

    {
        AuditLog log(path);
        CHECK_THROWS(log.append("two\nlines"));
        log.append("first event");
        log.append("second event");
    }
    {
        // Reopen: the chain must continue from the tail on disk.
        AuditLog log(path);
        log.append("third event");
    }
    CHECK(AuditLog::verify(path) == 3);

    // Flip one byte of recorded history: every later link inherits the
    // lie, so the file as a whole must stop verifying.
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(70);
    f.put('X');
    f.close();
    CHECK(!AuditLog::verify(path));

    std::filesystem::remove(path);
}

TEST_CASE("keyd proposals: squatted pipe name refuses the trust plane")
{
    const std::string vaultPath = make_test_vault("correct horse");
    izan::ipc::NamedPipeServer squatter("izan.test.keyd.squat");
    CHECK_THROWS(
        KeydClient::spawn(self_exe(), vaultPath, "", "izan.test.keyd.squat"));
    std::filesystem::remove(vaultPath);
}

TEST_CASE("keyd proposals: submission, provenance, passphrase-gated verdicts")
{
    const std::string vaultPath = make_test_vault("correct horse");
    const std::string auditPath = temp_file("proposals_keyd.audit");
    std::filesystem::remove(auditPath);

    KeydClient keyd = KeydClient::spawn(self_exe(), vaultPath, auditPath);
    const std::string pipe = keyd.pipe_name();
    const std::vector<uint8_t> payload { 't', 'x', '1' };

    // A stranger process may submit — that is the product — but only
    // into the pending queue.
    auto reply = raw_transact(pipe, submit_frame(0, payload));
    REQUIRE(reply);
    REQUIRE(reply->size() == 9);
    REQUIRE((*reply)[0] == uint8_t(PipeOp::Accepted));
    const uint64_t anonId = get_u64le(reply->data() + 1);
    CHECK(query_state(pipe, anonId) == ProposalState::Pending);

    // Claiming UI provenance without the session subkey must fail
    // loudly, not degrade to anonymous.
    std::vector<uint8_t> forged { uint8_t(PipeOp::Submit), 1 };
    forged.resize(2 + kProposalMacBytes, 0xAB);
    forged.insert(forged.end(), payload.begin(), payload.end());
    auto forgedReply = raw_transact(pipe, forged);
    REQUIRE(forgedReply);
    CHECK((*forgedReply)[0] == uint8_t(PipeOp::Rejected));

    // An empty proposal is noise, not a decision to put to a human.
    auto emptyReply = raw_transact(pipe, submit_frame(0, {}));
    REQUIRE(emptyReply);
    CHECK((*emptyReply)[0] == uint8_t(PipeOp::Rejected));

    // The real UI's submission carries the MAC and shows up marked.
    auto uiId = keyd.submit_ui(payload);
    REQUIRE(uiId);

    auto pending = keyd.pending();
    REQUIRE(pending);
    REQUIRE(pending->size() == 2);
    CHECK((*pending)[0].id == anonId);
    CHECK((*pending)[0].provenance == Provenance::Anonymous);
    CHECK((*pending)[1].id == *uiId);
    CHECK((*pending)[1].provenance == Provenance::Ui);

    auto fetched = keyd.fetch(anonId);
    REQUIRE(fetched);
    CHECK(fetched->first == Provenance::Anonymous);
    CHECK(fetched->second == payload);

    // §3.1 gap one: a click is not consent. Approval spends the
    // passphrase, and a wrong one moves nothing.
    CHECK(!keyd.approve(anonId, sb_from("wrong pass")));
    CHECK(keyd.last_error() == "bad passphrase");
    CHECK(query_state(pipe, anonId) == ProposalState::Pending);

    // Approval IS the signature. Recover it to prove which key spoke:
    // the vault seed's account #0, over exactly the queue's bytes.
    uint8_t digest[32];
    keccak_256(payload.data(), payload.size(), digest);
    auto sig = keyd.approve(anonId, sb_from("correct horse"));
    REQUIRE(sig);
    CHECK(recovered_signer(*sig, digest) == kDevAccount0);
    CHECK(query_state(pipe, anonId) == ProposalState::Approved);
    CHECK(!keyd.approve(anonId, sb_from("correct horse"))); // final
    CHECK(keyd.last_error() == "unknown proposal");

    CHECK(keyd.deny(*uiId));
    CHECK(query_state(pipe, *uiId) == ProposalState::Denied);
    CHECK(!keyd.deny(9999));

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    // Every consequential moment above must have left a verifiable
    // line: two submits, one forged-MAC alarm, one bad passphrase, one
    // approval, one denial.
    CHECK(AuditLog::verify(auditPath) == 6);

    // The approval record commits to what was signed and by whom —
    // the countable leg of the sign ≡ approve ≡ audit identity.
    {
        std::ifstream lf(auditPath, std::ios::binary);
        std::stringstream ls;
        ls << lf.rdbuf();
        char digestHex[65];
        sodium_bin2hex(digestHex, sizeof digestHex, digest, sizeof digest);
        CHECK(ls.str().find(std::string("digest=") + digestHex)
            != std::string::npos);
        CHECK(ls.str().find(std::string("signer=") + kDevAccount0)
            != std::string::npos);
    }
    std::fstream f(auditPath, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(70);
    f.put('X');
    f.close();
    CHECK(!AuditLog::verify(auditPath));

    std::filesystem::remove(auditPath);
    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".bak");
}

TEST_CASE("signer: seed to signature, straight through")
{
    const SecureBytes entropy = izan::crypto::mnemonic_to_entropy(kDevMnemonic);
    const std::vector<uint8_t> payload { 0x02, 0xc0, 0xff, 0xee };

    const SignedDigest out = sign_payload(entropy, payload);
    CHECK(out.signer == kDevAccount0);

    uint8_t digest[32];
    keccak_256(payload.data(), payload.size(), digest);
    CHECK(std::memcmp(out.digest.data(), digest, 32) == 0);

    ApprovedSignature wire;
    wire.y_parity = out.sig.y_parity;
    wire.r = out.sig.r;
    wire.s = out.sig.s;
    CHECK(recovered_signer(wire, digest) == kDevAccount0);

    CHECK_THROWS_AS(
        sign_payload(entropy, std::vector<uint8_t> {}), std::invalid_argument);
    CHECK_THROWS_AS(
        sign_payload(SecureBytes(), payload), std::invalid_argument);
}

TEST_CASE("keyd proposals: a seedless vault approves nothing")
{
    // A vault holding only imported keys has no seed to derive from;
    // approval must refuse — and crucially leave the proposal pending,
    // not half-approved with no signature to show for it.
    const std::string vaultPath = temp_file("proposals_seedless.qvlt");
    izan::vault::Wallet wallet;
    izan::vault::save(
        vaultPath, sb_from("correct horse"), wallet, izan::vault::kdf_min());

    KeydClient keyd = KeydClient::spawn(self_exe(), vaultPath);
    auto id = keyd.submit_ui({ 't', 'x' });
    REQUIRE(id);
    CHECK(!keyd.approve(*id, sb_from("correct horse")));
    CHECK(keyd.last_error() == "signer: vault holds no seed");
    CHECK(query_state(keyd.pipe_name(), *id) == ProposalState::Pending);

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".audit");
}

#endif
