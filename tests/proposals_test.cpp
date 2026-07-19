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
// Upstream header trips C++20's volatile-parameter deprecation; not ours
// to fix, not ours to drown in either.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include <base58.h>
#include <ecdsa.h>
#include <ed25519-donna/ed25519.h>
#include <secp256k1.h>
#include <sha3.h>
#pragma GCC diagnostic pop
}

#include "core/crypto/bip39.hpp"
#include "core/crypto/eth.hpp"
#include "core/secure/vault.hpp"
#include "domain/sol/sol_tx.hpp"
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

std::vector<uint8_t> unhex_local(std::string_view hex)
{
    auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9')
            return uint8_t(c - '0');
        return uint8_t(c - 'a' + 10);
    };
    std::vector<uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(uint8_t(nib(hex[i]) << 4 | nib(hex[i + 1])));
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

    // An enveloped proposal signs as the enclosed account: derive the
    // wallet's account #1 through keyd and check the signature lands
    // there, not on account #0.
    REQUIRE(keyd.unlock(sb_from("correct horse")));
    auto addr1 = keyd.address(1);
    REQUIRE(addr1);
    std::vector<uint8_t> enveloped { kEnvelopeV1, 0x01, 0x00, 0x00, 0x00 };
    const std::vector<uint8_t> innerTx { 't', 'x', '9' };
    enveloped.insert(enveloped.end(), innerTx.begin(), innerTx.end());
    auto envId = keyd.submit_ui(enveloped);
    REQUIRE(envId);
    uint8_t envDigest[32];
    keccak_256(innerTx.data(), innerTx.size(), envDigest);
    auto envSig = keyd.approve(*envId, sb_from("correct horse"));
    REQUIRE(envSig);
    CHECK(recovered_signer(*envSig, envDigest) == *addr1);
    CHECK(*addr1 != kDevAccount0);

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    // Every consequential moment above must have left a verifiable
    // line: three submits, one forged-MAC alarm, one bad passphrase,
    // two approvals, one denial.
    CHECK(AuditLog::verify(auditPath) == 8);

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

TEST_CASE("signer: the wallet's contents choose the key")
{
    const std::vector<uint8_t> payload { 0x02, 0xc0, 0xff, 0xee };
    uint8_t digest[32];
    keccak_256(payload.data(), payload.size(), digest);

    // Seed wallet: derive account #0.
    izan::vault::Wallet seed;
    seed.entropy = izan::crypto::mnemonic_to_entropy(kDevMnemonic);
    const SignedDigest bySeed = sign_payload(seed, payload);
    CHECK(bySeed.signer == kDevAccount0);
    CHECK(std::memcmp(bySeed.digest.data(), digest, 32) == 0);
    ApprovedSignature wire;
    wire.y_parity = bySeed.sig.y_parity;
    wire.r = bySeed.sig.r;
    wire.s = bySeed.sig.s;
    CHECK(recovered_signer(wire, digest) == kDevAccount0);
    CHECK(account_address(seed) == kDevAccount0);

    // Key-only wallet holding the same account's raw key: identical
    // identity through the other path — no derivation involved.
    izan::vault::Wallet keyed;
    izan::vault::Imported imp;
    imp.label = "dev";
    const std::vector<uint8_t> raw = unhex_local(
        "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80");
    imp.key = SecureBytes(32);
    std::memcpy(imp.key.data(), raw.data(), 32);
    keyed.imported.push_back(std::move(imp));
    const SignedDigest byKey = sign_payload(keyed, payload);
    CHECK(byKey.signer == kDevAccount0);
    CHECK(account_address(keyed) == kDevAccount0);
    wire.y_parity = byKey.sig.y_parity;
    wire.r = byKey.sig.r;
    wire.s = byKey.sig.s;
    CHECK(recovered_signer(wire, digest) == kDevAccount0);

    // HD means endless addresses: index 1 lands on the second Hardhat
    // account, and the signature speaks for it.
    const char* kDevAccount1 = "0x70997970C51812dc3A010C7d01b50e0d17dc79C8";
    CHECK(account_address(seed, 1) == kDevAccount1);
    const SignedDigest byIdx = sign_payload(seed, payload, 1);
    CHECK(byIdx.signer == kDevAccount1);
    wire.y_parity = byIdx.sig.y_parity;
    wire.r = byIdx.sig.r;
    wire.s = byIdx.sig.s;
    CHECK(recovered_signer(wire, digest) == kDevAccount1);

    // A key wallet has exactly one identity; other indexes are lies.
    CHECK_THROWS_AS(account_address(keyed, 1), std::invalid_argument);
    CHECK_THROWS_AS(sign_payload(keyed, payload, 1), std::invalid_argument);

    // Envelope parsing: versioned prefix carries the account, bare
    // payloads stay account 0, truncation refuses.
    {
        std::vector<uint8_t> enveloped { izan::keyd::kEnvelopeV1, 0x07, 0x00,
            0x00, 0x00, 0xAA, 0xBB };
        const auto body = izan::keyd::parse_proposal(enveloped);
        CHECK(body.account == 7);
        REQUIRE(body.tx.size() == 2);
        CHECK(body.tx[0] == 0xAA);
        const auto bare = izan::keyd::parse_proposal(payload);
        CHECK(bare.account == 0);
        CHECK(bare.tx.size() == payload.size());
        const std::vector<uint8_t> truncated { izan::keyd::kEnvelopeV1, 0x01 };
        CHECK_THROWS(izan::keyd::parse_proposal(truncated));
        const std::vector<uint8_t> empty_tx { izan::keyd::kEnvelopeV1, 0x00,
            0x00, 0x00, 0x00 };
        CHECK_THROWS(izan::keyd::parse_proposal(empty_tx));
    }

    // Derivation presets: each maps an index to its vendor's path, and
    // different presets are different identities from the same seed.
    CHECK(izan::keyd::derive_path(DerivePreset::MetaMask, 7)
        == "m/44'/60'/0'/0/7");
    CHECK(izan::keyd::derive_path(DerivePreset::LedgerLive, 7)
        == "m/44'/60'/7'/0/0");
    CHECK(izan::keyd::derive_path(DerivePreset::LegacyMew, 7)
        == "m/44'/60'/0'/7");
    CHECK(account_address(seed, 0, DerivePreset::MetaMask) == kDevAccount0);
    // At index 0 MetaMask and Ledger Live agree by construction (both
    // are m/44'/60'/0'/0/0); the presets fork from index 1 on.
    CHECK(account_address(seed, 0, DerivePreset::LedgerLive) == kDevAccount0);
    const std::string ledger1
        = account_address(seed, 1, DerivePreset::LedgerLive);
    CHECK(ledger1 != account_address(seed, 1, DerivePreset::MetaMask));
    CHECK(ledger1 != account_address(seed, 1, DerivePreset::LegacyMew));
    const SignedDigest byLedger
        = sign_payload(seed, payload, 1, DerivePreset::LedgerLive);
    CHECK(byLedger.signer == ledger1);

    // Envelope v2 carries the preset; a byte outside the registry is
    // refused, never silently derived.
    {
        std::vector<uint8_t> v2 { izan::keyd::kEnvelopeV2, 0x01, 0x03, 0x00,
            0x00, 0x00, 0xAA };
        const auto body = izan::keyd::parse_proposal(v2);
        CHECK(body.preset == DerivePreset::LedgerLive);
        CHECK(body.account == 3);
        REQUIRE(body.tx.size() == 1);
        const std::vector<uint8_t> badPreset { izan::keyd::kEnvelopeV2, 0x09,
            0x00, 0x00, 0x00, 0x00, 0xAA };
        CHECK_THROWS(izan::keyd::parse_proposal(badPreset));
    }

    // An empty wallet has nothing to say.
    CHECK_THROWS_AS(
        sign_payload(seed, std::vector<uint8_t> {}), std::invalid_argument);
    CHECK_THROWS_AS(
        sign_payload(izan::vault::Wallet {}, payload), std::invalid_argument);
    CHECK_THROWS_AS(
        account_address(izan::vault::Wallet {}), std::invalid_argument);
}

TEST_CASE("keyd: the address answers only while unlocked")
{
    const std::string vaultPath = make_test_vault("correct horse");
    KeydClient keyd = KeydClient::spawn(self_exe(), vaultPath);

    // A locked keyd tells nobody what it guards.
    CHECK(!keyd.address());
    CHECK(keyd.last_error() == "locked");

    REQUIRE(keyd.unlock(sb_from("correct horse")));
    auto addr = keyd.address();
    REQUIRE(addr);
    CHECK(*addr == kDevAccount0);

    REQUIRE(keyd.lock());
    CHECK(!keyd.address());

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".audit");
}

TEST_CASE("keyd proposals: an empty vault approves nothing")
{
    // No seed, no keys: approval must refuse — and crucially leave the
    // proposal pending, not half-approved with no signature to show
    // for it.
    const std::string vaultPath = temp_file("proposals_empty.qvlt");
    izan::vault::Wallet wallet;
    izan::vault::save(
        vaultPath, sb_from("correct horse"), wallet, izan::vault::kdf_min());

    KeydClient keyd = KeydClient::spawn(self_exe(), vaultPath);
    auto id = keyd.submit_ui({ 't', 'x' });
    REQUIRE(id);
    CHECK(!keyd.approve(*id, sb_from("correct horse")));
    CHECK(keyd.last_error() == "signer: wallet holds no signing key");
    CHECK(query_state(keyd.pipe_name(), *id) == ProposalState::Pending);

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".audit");
}

TEST_CASE("keyd proposals: a key-only wallet signs and reveals its key")
{
    const std::string vaultPath = temp_file("proposals_keyonly.qvlt");
    izan::vault::Wallet wallet;
    izan::vault::Imported imp;
    imp.label = "dev";
    const std::vector<uint8_t> raw = unhex_local(
        "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80");
    imp.key = SecureBytes(32);
    std::memcpy(imp.key.data(), raw.data(), 32);
    wallet.imported.push_back(std::move(imp));
    izan::vault::save(
        vaultPath, sb_from("correct horse"), wallet, izan::vault::kdf_min());

    KeydClient keyd = KeydClient::spawn(self_exe(), vaultPath);

    // Address answers with the key's own identity while unlocked, and
    // reports what kind of wallet is speaking.
    REQUIRE(keyd.unlock(sb_from("correct horse")));
    auto addr = keyd.address();
    REQUIRE(addr);
    CHECK(*addr == kDevAccount0);
    CHECK(keyd.wallet_kind() == RevealKind::PrivateKey);

    // Approval signs through the raw-key path.
    const std::vector<uint8_t> payload { 'k', 'e', 'y' };
    auto id = keyd.submit_ui(payload);
    REQUIRE(id);
    uint8_t digest[32];
    keccak_256(payload.data(), payload.size(), digest);
    auto sig = keyd.approve(*id, sb_from("correct horse"));
    REQUIRE(sig);
    CHECK(recovered_signer(*sig, digest) == kDevAccount0);

    // Backup reveals the key itself, marked as such.
    auto revealed = keyd.reveal(sb_from("correct horse"));
    REQUIRE(revealed);
    CHECK(revealed->kind == RevealKind::PrivateKey);
    REQUIRE(revealed->secret.size() == 32);
    CHECK(std::memcmp(revealed->secret.data(), raw.data(), 32) == 0);

    CHECK(keyd.shutdown());
    auto exit = keyd.wait_exit(5000);
    REQUIRE(exit);
    CHECK(*exit == 0);

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(vaultPath + ".audit");
    std::filesystem::remove(vaultPath + ".bak");
}

#endif

TEST_CASE("keyd proposals: a solana transfer signs, anything else is refused")
{
    const std::string vaultPath = make_test_vault("correct horse");
    const std::string auditPath = temp_file("proposals_sol.audit");
    std::filesystem::remove(auditPath);
    KeydClient keyd = KeydClient::spawn(self_exe(), vaultPath, auditPath);
    REQUIRE(keyd.unlock(sb_from("correct horse")));

    const uint8_t solPreset = uint8_t(izan::keyd::DerivePreset::SolPhantom);
    const auto from = keyd.address(0, solPreset);
    REQUIRE(from);
    std::array<uint8_t, 32> hash {};
    hash.fill(0x11);
    const auto msg = izan::sol::encode_transfer_message(
        *from, "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v", 5000, hash);
    const auto env = izan::keyd::make_envelope(
        izan::keyd::DerivePreset::SolPhantom, 0, msg);
    const auto id = keyd.submit_ui(env);
    REQUIRE(id);

    const auto sig = keyd.approve_sol(*id, sb_from("correct horse"));
    REQUIRE(sig);
    // The signature verifies against the account's own public key —
    // the base58 address decoded back to its 32 bytes.
    uint8_t pub[32];
    std::size_t pubSize = sizeof pub;
    REQUIRE(b58tobin(pub, &pubSize, from->c_str()));
    CHECK(ed25519_sign_open(msg.data(), msg.size(), pub, sig->data()) == 0);
    // Once signed, spent — for either twin.
    CHECK(!keyd.approve_sol(*id, sb_from("correct horse")));
    CHECK(!keyd.approve(*id, sb_from("correct horse")));

    // The whitelist: bytes the approval screen cannot read as a bare
    // transfer never get a signature, however valid the passphrase.
    const std::vector<uint8_t> junk { 0xde, 0xad, 0xbe, 0xef };
    const auto badId = keyd.submit_ui(izan::keyd::make_envelope(
        izan::keyd::DerivePreset::SolPhantom, 0, junk));
    REQUIRE(badId);
    CHECK(!keyd.approve_sol(*badId, sb_from("correct horse")));
    CHECK(query_state(keyd.pipe_name(), *badId) == ProposalState::Pending);
}
