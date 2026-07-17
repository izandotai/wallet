#include "keyd/child.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sodium.h>

#include "core/secure/secure_bytes.hpp"
#include "core/secure/vault.hpp"
#include "keyd/audit.hpp"
#include "keyd/autolock.hpp"
#include "keyd/hardening.hpp"
#include "keyd/proposals.hpp"
#include "keyd/protocol.hpp"
#include "keyd/signer.hpp"
#include "platform/ipc/named_pipe.hpp"
#include "platform/ipc/secret_channel.hpp"

namespace izan::keyd {

namespace {

    using secure::SecureBytes;

    uint64_t token_arg(std::string_view arg)
    {
        uint64_t value = 0;
        std::from_chars(arg.data(), arg.data() + arg.size(), value);
        return value;
    }

    bool send_op(ipc::SecretChannel& ch, Op op, const uint8_t* body = nullptr,
        std::size_t size = 0)
    {
        std::vector<uint8_t> frame(1 + size);
        frame[0] = uint8_t(op);
        if (size)
            std::memcpy(frame.data() + 1, body, size);
        return ch.send(frame.data(), frame.size());
    }

    bool send_err(ipc::SecretChannel& ch, std::string_view reason)
    {
        std::vector<uint8_t> frame(1 + reason.size());
        frame[0] = uint8_t(Op::Err);
        std::memcpy(frame.data() + 1, reason.data(), reason.size());
        return ch.send(frame.data(), frame.size());
    }

    // The unlocked wallet, shared between the password loop and the
    // autolock watcher — the one writer that may take it away from
    // under the loop.
    struct WalletHolder {
        std::mutex mutex;
        std::optional<vault::Wallet> wallet;
    };

    // Everything the proposal-pipe thread shares with the password
    // loop. Heap-held behind shared_ptr because the thread is detached:
    // on shutdown the process exits while the thread may still be
    // blocked in ConnectNamedPipe, and it must not be left holding
    // references into a dead stack frame.
    struct ProposalPlane {
        ipc::NamedPipeServer server;
        ProposalQueue queue;
        AuditLog audit;
        SecureBytes mac_key;

        ProposalPlane(
            const std::string& pipe, std::string audit_path, SecureBytes key)
            : server(pipe)
            , audit(std::move(audit_path))
            , mac_key(std::move(key))
        {
        }
    };

    void reject(ipc::NamedPipeServer& server, std::string_view reason)
    {
        std::vector<uint8_t> frame(1 + reason.size());
        frame[0] = uint8_t(PipeOp::Rejected);
        std::memcpy(frame.data() + 1, reason.data(), reason.size());
        server.write_message(frame.data(), frame.size());
    }

    void handle_submit(ProposalPlane& plane, const std::vector<uint8_t>& msg)
    {
        // The auto-approve zone ships welded shut: a submission's only
        // possible destination is the pending queue.
        static_assert(!Policy::kAutoApproveEnabled);

        if (msg.size() < 2)
            return reject(plane.server, "short frame");
        const uint8_t flag = msg[1];
        std::size_t payloadAt = 2;
        Provenance prov = Provenance::Anonymous;
        if (flag == 1) {
            if (msg.size() < 2 + kProposalMacBytes)
                return reject(plane.server, "short frame");
            payloadAt = 2 + kProposalMacBytes;
            // A claim of UI provenance must verify or fail loudly;
            // silently downgrading a forged MAC to anonymous would
            // bury the one signal that someone is impersonating the
            // UI.
            if (crypto_auth_verify(msg.data() + 2, msg.data() + payloadAt,
                    msg.size() - payloadAt, plane.mac_key.data())
                != 0) {
                plane.audit.append("proposal.submit.badmac");
                return reject(plane.server, "bad mac");
            }
            prov = Provenance::Ui;
        } else if (flag != 0) {
            return reject(plane.server, "bad flag");
        }
        if (msg.size() == payloadAt)
            return reject(plane.server, "empty proposal");

        std::vector<uint8_t> payload(
            msg.begin() + std::ptrdiff_t(payloadAt), msg.end());
        const uint64_t id = plane.queue.submit(std::move(payload), prov);
        plane.audit.append("proposal.submit id=" + std::to_string(id)
            + " prov=" + (prov == Provenance::Ui ? "ui" : "anon")
            + " bytes=" + std::to_string(msg.size() - payloadAt));

        uint8_t out[9];
        out[0] = uint8_t(PipeOp::Accepted);
        put_u64le(out + 1, id);
        plane.server.write_message(out, sizeof out);
    }

    void handle_query(ProposalPlane& plane, const std::vector<uint8_t>& msg)
    {
        if (msg.size() != 9)
            return reject(plane.server, "short frame");
        const uint8_t out[2] = { uint8_t(PipeOp::QueryState),
            uint8_t(plane.queue.state(get_u64le(msg.data() + 1))) };
        plane.server.write_message(out, sizeof out);
    }

    // One persistent instance, clients served strictly in turn. Runs
    // until the process exits; holds no vault material — the worst a
    // hostile client can achieve here is a queue entry a human will
    // squint at.
    void serve_proposals(std::shared_ptr<ProposalPlane> plane)
    {
        while (plane->server.wait_client()) {
            for (;;) {
                std::optional<std::vector<uint8_t>> msg
                    = plane->server.read_message();
                if (!msg)
                    break;
                if (msg->empty())
                    continue;
                switch (PipeOp((*msg)[0])) {
                case PipeOp::Submit:
                    handle_submit(*plane, *msg);
                    break;
                case PipeOp::Query:
                    handle_query(*plane, *msg);
                    break;
                default:
                    reject(plane->server, "unknown op");
                }
            }
            plane->server.drop_client();
        }
    }

}

int child_main(int argc, char** argv)
{
    // Before anything secret can exist in this process.
    uint8_t hardened = apply_process_hardening();

    uint64_t inTok = 0, outTok = 0, keyTok = 0;
    std::string vaultPath, pipeName, auditPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.starts_with("--in="))
            inTok = token_arg(arg.substr(5));
        else if (arg.starts_with("--out="))
            outTok = token_arg(arg.substr(6));
        else if (arg.starts_with("--key="))
            keyTok = token_arg(arg.substr(6));
        else if (arg.starts_with("--vault="))
            vaultPath = std::string(arg.substr(8));
        else if (arg.starts_with("--proposals="))
            pipeName = std::string(arg.substr(12));
        else if (arg.starts_with("--audit="))
            auditPath = std::string(arg.substr(8));
    }
    if (!inTok || !outTok || !keyTok || vaultPath.empty() || pipeName.empty()
        || auditPath.empty())
        return 2;

    // One-shot session key, then the delivery pipe dies.
    SecureBytes key(ipc::SecretChannel::kKeyBytes);
    {
        ipc::PipeEnd keyPipe = ipc::PipeEnd::from_token(keyTok);
        if (!keyPipe.read_all(key.data(), key.size()))
            return 2;
    }

    try {
        // Domain-separated subkey for proposal MACs; the raw session
        // key itself never outlives channel setup.
        SecureBytes macKey(crypto_auth_KEYBYTES);
        static const char kMacDomain[] = "izan.keyd.proposal-mac.v1";
        crypto_generichash(macKey.data(), macKey.size(),
            reinterpret_cast<const uint8_t*>(kMacDomain), sizeof kMacDomain - 1,
            key.data(), key.size());

        ipc::SecretChannel channel(ipc::PipeEnd::from_token(inTok),
            ipc::PipeEnd::from_token(outTok), key);
        key.reset();

        // The pipe grabs its name before Hello: if a squatter owns it,
        // the parent sees a child that died instead of a trust plane
        // whose front door belongs to someone else.
        auto plane = std::make_shared<ProposalPlane>(
            pipeName, auditPath, std::move(macKey));
        std::thread(serve_proposals, plane).detach();

        auto holder = std::make_shared<WalletHolder>();
        if (start_autolock_watch(pipeName, [holder, plane](const char* why) {
                std::lock_guard lock(holder->mutex);
                if (holder->wallet) {
                    holder->wallet.reset();
                    plane->audit.append(std::string("autolock reason=") + why);
                }
            }))
            hardened |= kHardenedAutoLock;

        const uint8_t hello[2] = { kProtocolVersion, hardened };
        if (!send_op(channel, Op::Hello, hello, sizeof hello))
            return 0;

        // One meter for every passphrase-spending operation. Sitting in
        // this process means killing and respawning the UI does not
        // reset it.
        int badPass = 0;
        const auto throttle_bad_pass = [&badPass] {
            if (badPass >= kPassThrottleAfter)
                std::this_thread::sleep_for(std::chrono::seconds(
                    std::min(badPass - kPassThrottleAfter + 1,
                        kPassThrottleCapSeconds)));
        };

        for (;;) {
            std::optional<SecureBytes> frame = channel.recv();
            if (!frame)
                break; // parent gone: fall through to wipe and exit
            if (frame->empty())
                continue;
            const Op op = Op(frame->data()[0]);
            switch (op) {
            case Op::Unlock: {
                throttle_bad_pass();
                SecureBytes pass(frame->size() - 1);
                if (!pass.empty())
                    std::memcpy(pass.data(), frame->data() + 1, pass.size());
                try {
                    vault::Wallet opened = vault::open(vaultPath, pass);
                    badPass = 0;
                    std::lock_guard lock(holder->mutex);
                    holder->wallet = std::move(opened);
                    send_op(channel, Op::Ok);
                } catch (const std::exception& e) {
                    ++badPass;
                    std::lock_guard lock(holder->mutex);
                    holder->wallet.reset();
                    send_err(channel, e.what());
                }
                break;
            }
            case Op::Lock: {
                std::lock_guard lock(holder->mutex);
                holder->wallet.reset();
                send_op(channel, Op::Ok);
                break;
            }
            case Op::Status: {
                std::lock_guard lock(holder->mutex);
                const uint8_t state = holder->wallet.has_value() ? 1 : 0;
                send_op(channel, Op::State, &state, 1);
                break;
            }
            case Op::Approve: {
                // §3.1 gap one: approving spends the passphrase, not a
                // click. Verify the human first, only then touch the
                // proposal's state. Approval and signing are one act,
                // over the queue's own bytes: what the human saw at
                // Fetch is what gets signed, whatever the UI became in
                // between.
                if (frame->size() < 1 + 8) {
                    send_err(channel, "short frame");
                    break;
                }
                throttle_bad_pass();
                const uint64_t id = get_u64le(frame->data() + 1);
                SecureBytes pass(frame->size() - 9);
                if (!pass.empty())
                    std::memcpy(pass.data(), frame->data() + 9, pass.size());
                std::optional<vault::Wallet> opened;
                try {
                    opened = vault::open(vaultPath, pass);
                    badPass = 0;
                } catch (const std::exception&) {
                    ++badPass;
                    plane->audit.append(
                        "proposal.approve.badpass id=" + std::to_string(id));
                    send_err(channel, "bad passphrase");
                    break;
                }
                const std::optional<Proposal> p = plane->queue.get(id);
                if (!p || p->state != ProposalState::Pending) {
                    send_err(channel, "unknown proposal");
                    break;
                }
                SignedDigest signature;
                try {
                    signature = sign_payload(opened->entropy, p->payload);
                } catch (const std::exception& e) {
                    plane->audit.append(
                        "proposal.sign.fail id=" + std::to_string(id));
                    send_err(channel, e.what());
                    break;
                }
                opened.reset();
                // Single-threaded verdicts: the resolve cannot lose a
                // race, so a signature never outlives a still-pending
                // state.
                plane->queue.resolve(id, ProposalState::Approved);
                char digestHex[65];
                sodium_bin2hex(digestHex, sizeof digestHex,
                    signature.digest.data(), signature.digest.size());
                // One record per signature — the countable third leg of
                // the sign ≡ approve ≡ audit identity.
                plane->audit.append("proposal.approve id=" + std::to_string(id)
                    + " digest=" + digestHex + " signer=" + signature.signer);
                uint8_t body[kSignedBodyBytes];
                body[0] = signature.sig.y_parity;
                std::memcpy(body + 1, signature.sig.r.data(), 32);
                std::memcpy(body + 33, signature.sig.s.data(), 32);
                send_op(channel, Op::Signed, body, sizeof body);
                break;
            }
            case Op::Deny: {
                if (frame->size() != 1 + 8) {
                    send_err(channel, "short frame");
                    break;
                }
                const uint64_t id = get_u64le(frame->data() + 1);
                if (!plane->queue.resolve(id, ProposalState::Denied)) {
                    send_err(channel, "unknown proposal");
                    break;
                }
                plane->audit.append("proposal.deny id=" + std::to_string(id));
                send_op(channel, Op::Ok);
                break;
            }
            case Op::Pending: {
                const std::vector<uint64_t> ids = plane->queue.pending_ids();
                std::vector<uint8_t> body;
                body.reserve(ids.size() * 9);
                for (const uint64_t id : ids) {
                    uint8_t rec[9];
                    put_u64le(rec, id);
                    rec[8] = uint8_t(plane->queue.get(id)->provenance);
                    body.insert(body.end(), rec, rec + sizeof rec);
                }
                send_op(channel, Op::PendingList, body.data(), body.size());
                break;
            }
            case Op::Fetch: {
                if (frame->size() != 1 + 8) {
                    send_err(channel, "short frame");
                    break;
                }
                std::optional<Proposal> p
                    = plane->queue.get(get_u64le(frame->data() + 1));
                if (!p) {
                    send_err(channel, "unknown proposal");
                    break;
                }
                std::vector<uint8_t> body(1 + p->payload.size());
                body[0] = uint8_t(p->provenance);
                std::memcpy(
                    body.data() + 1, p->payload.data(), p->payload.size());
                send_op(channel, Op::ProposalBody, body.data(), body.size());
                break;
            }
            case Op::Reveal: {
                throttle_bad_pass();
                SecureBytes pass(frame->size() - 1);
                if (!pass.empty())
                    std::memcpy(pass.data(), frame->data() + 1, pass.size());
                try {
                    vault::Wallet opened = vault::open(vaultPath, pass);
                    badPass = 0;
                    if (opened.entropy.empty()) {
                        send_err(channel, "no seed in vault");
                        break;
                    }
                    plane->audit.append("vault.reveal");
                    std::vector<uint8_t> out(1 + opened.entropy.size());
                    out[0] = uint8_t(Op::Entropy);
                    std::memcpy(out.data() + 1, opened.entropy.data(),
                        opened.entropy.size());
                    channel.send(out.data(), out.size());
                    sodium_memzero(out.data(), out.size());
                } catch (const std::exception&) {
                    ++badPass;
                    plane->audit.append("vault.reveal.badpass");
                    send_err(channel, "bad passphrase");
                }
                break;
            }
            case Op::Shutdown: {
                std::lock_guard lock(holder->mutex);
                holder->wallet.reset();
                send_op(channel, Op::Ok);
                return 0;
            }
            default:
                send_err(channel, "unknown op");
            }
        }
    } catch (...) {
        // Tampered frame or transport failure: SecureBytes members
        // wipe on unwind; never limp along.
        return 3;
    }
    return 0;
}

}
