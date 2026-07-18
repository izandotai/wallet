#include "keyd/client.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

#include <sodium.h>
#include <windows.h>

#include "keyd/protocol.hpp"
#include "platform/ipc/named_pipe.hpp"
#include "platform/ipc/pipe.hpp"

namespace izan::keyd {

using secure::SecureBytes;

namespace {

    std::string random_pipe_name()
    {
        uint8_t raw[8];
        randombytes_buf(raw, sizeof raw);
        char hex[17];
        sodium_bin2hex(hex, sizeof hex, raw, sizeof raw);
        return "izan.keyd." + std::to_string(GetCurrentProcessId()) + "." + hex;
    }

}

KeydClient KeydClient::spawn(const std::string& exe,
    const std::string& vault_path, const std::string& audit_path,
    const std::string& pipe_name)
{
    // Child-facing ends inheritable, parent ends private.
    ipc::Pipe toChild = ipc::make_pipe(true, false);   // child reads
    ipc::Pipe fromChild = ipc::make_pipe(false, true); // child writes
    ipc::Pipe keyPipe = ipc::make_pipe(true, false);

    SecureBytes key(ipc::SecretChannel::kKeyBytes);
    randombytes_buf(key.data(), key.size());

    KeydClient client;
    client.m_pipe_name = pipe_name.empty() ? random_pipe_name() : pipe_name;
    const std::string auditPath
        = audit_path.empty() ? vault_path + ".audit" : audit_path;

    std::string cmd = "\"" + exe
        + "\" --keyd-child --in=" + std::to_string(toChild.read.token())
        + " --out=" + std::to_string(fromChild.write.token())
        + " --key=" + std::to_string(keyPipe.read.token()) + " --vault=\""
        + vault_path + "\" --proposals=" + client.m_pipe_name + " --audit=\""
        + auditPath + "\"";

    void* inherit[3] = { toChild.read.handle(), fromChild.write.handle(),
        keyPipe.read.handle() };
    client.m_child = proc::ChildProcess::spawn(cmd, inherit);

    // Drop the child-side ends in this process, hand over the key,
    // then forget it — the channel states and the proposal-MAC subkey
    // are the only derivatives left.
    toChild.read.close();
    fromChild.write.close();
    keyPipe.read.close();
    if (!keyPipe.write.write_all(key.data(), key.size()))
        throw std::runtime_error("keyd: key handover failed");
    keyPipe.write.close();

    client.m_mac_key = SecureBytes(crypto_auth_KEYBYTES);
    static const char kMacDomain[] = "izan.keyd.proposal-mac.v1";
    crypto_generichash(client.m_mac_key.data(), client.m_mac_key.size(),
        reinterpret_cast<const uint8_t*>(kMacDomain), sizeof kMacDomain - 1,
        key.data(), key.size());

    client.m_channel = std::make_unique<ipc::SecretChannel>(
        std::move(fromChild.read), std::move(toChild.write), key);
    key.reset();

    std::optional<SecureBytes> hello = client.m_channel->recv();
    if (!hello || hello->size() < 3 || hello->data()[0] != uint8_t(Op::Hello))
        throw std::runtime_error("keyd: no hello from child");
    client.m_hello = { hello->data()[1], hello->data()[2] };
    return client;
}

std::optional<SecureBytes> KeydClient::request(
    const uint8_t* frame, std::size_t size)
{
    if (!m_channel || !m_channel->send(frame, size))
        return std::nullopt;
    return m_channel->recv();
}

bool KeydClient::unlock(const SecureBytes& passphrase)
{
    std::vector<uint8_t> frame(1 + passphrase.size());
    frame[0] = uint8_t(Op::Unlock);
    if (!passphrase.empty())
        std::memcpy(frame.data() + 1, passphrase.data(), passphrase.size());
    std::optional<SecureBytes> reply = request(frame.data(), frame.size());
    sodium_memzero(frame.data(), frame.size());
    if (!reply || reply->empty()) {
        m_last_error = "channel broken";
        return false;
    }
    if (reply->data()[0] == uint8_t(Op::Ok))
        return true;
    m_last_error.assign(
        reinterpret_cast<const char*>(reply->data()) + 1, reply->size() - 1);
    return false;
}

bool KeydClient::lock()
{
    const uint8_t frame[1] = { uint8_t(Op::Lock) };
    std::optional<SecureBytes> reply = request(frame, 1);
    return reply && !reply->empty() && reply->data()[0] == uint8_t(Op::Ok);
}

std::optional<bool> KeydClient::unlocked()
{
    const uint8_t frame[1] = { uint8_t(Op::Status) };
    std::optional<SecureBytes> reply = request(frame, 1);
    if (!reply || reply->size() < 2 || reply->data()[0] != uint8_t(Op::State))
        return std::nullopt;
    return reply->data()[1] == 1;
}

bool KeydClient::shutdown()
{
    const uint8_t frame[1] = { uint8_t(Op::Shutdown) };
    std::optional<SecureBytes> reply = request(frame, 1);
    return reply && !reply->empty() && reply->data()[0] == uint8_t(Op::Ok);
}

std::optional<std::string> KeydClient::address()
{
    const uint8_t frame[1] = { uint8_t(Op::Address) };
    std::optional<SecureBytes> reply = request(frame, 1);
    if (!reply || reply->empty()) {
        m_last_error = "channel broken";
        return std::nullopt;
    }
    if (reply->data()[0] != uint8_t(Op::AddressIs)) {
        m_last_error.assign(reinterpret_cast<const char*>(reply->data()) + 1,
            reply->size() - 1);
        return std::nullopt;
    }
    return std::string(
        reinterpret_cast<const char*>(reply->data()) + 1, reply->size() - 1);
}

std::optional<uint64_t> KeydClient::submit_ui(
    const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> frame(2 + kProposalMacBytes + payload.size());
    frame[0] = uint8_t(PipeOp::Submit);
    frame[1] = 1; // UI provenance, backed by the MAC
    crypto_auth(
        frame.data() + 2, payload.data(), payload.size(), m_mac_key.data());
    std::memcpy(
        frame.data() + 2 + kProposalMacBytes, payload.data(), payload.size());

    ipc::NamedPipeClient pipe(m_pipe_name);
    std::optional<std::vector<uint8_t>> reply
        = pipe.transact(frame.data(), frame.size());
    if (!reply || reply->size() != 9
        || (*reply)[0] != uint8_t(PipeOp::Accepted)) {
        m_last_error = "proposal not accepted";
        return std::nullopt;
    }
    return get_u64le(reply->data() + 1);
}

std::optional<std::vector<PendingItem>> KeydClient::pending()
{
    const uint8_t frame[1] = { uint8_t(Op::Pending) };
    std::optional<SecureBytes> reply = request(frame, 1);
    if (!reply || reply->empty() || reply->data()[0] != uint8_t(Op::PendingList)
        || (reply->size() - 1) % 9 != 0)
        return std::nullopt;
    std::vector<PendingItem> out;
    for (std::size_t at = 1; at < reply->size(); at += 9)
        out.push_back({ get_u64le(reply->data() + at),
            Provenance(reply->data()[at + 8]) });
    return out;
}

std::optional<std::pair<Provenance, std::vector<uint8_t>>> KeydClient::fetch(
    uint64_t id)
{
    uint8_t frame[9];
    frame[0] = uint8_t(Op::Fetch);
    put_u64le(frame + 1, id);
    std::optional<SecureBytes> reply = request(frame, sizeof frame);
    if (!reply || reply->size() < 2
        || reply->data()[0] != uint8_t(Op::ProposalBody))
        return std::nullopt;
    return std::make_pair(Provenance(reply->data()[1]),
        std::vector<uint8_t>(reply->data() + 2, reply->data() + reply->size()));
}

std::optional<ApprovedSignature> KeydClient::approve(
    uint64_t id, const SecureBytes& passphrase)
{
    std::vector<uint8_t> frame(9 + passphrase.size());
    frame[0] = uint8_t(Op::Approve);
    put_u64le(frame.data() + 1, id);
    if (!passphrase.empty())
        std::memcpy(frame.data() + 9, passphrase.data(), passphrase.size());
    std::optional<SecureBytes> reply = request(frame.data(), frame.size());
    sodium_memzero(frame.data(), frame.size());
    if (!reply || reply->empty()) {
        m_last_error = "channel broken";
        return std::nullopt;
    }
    if (reply->data()[0] != uint8_t(Op::Signed)
        || reply->size() != 1 + kSignedBodyBytes) {
        m_last_error.assign(reinterpret_cast<const char*>(reply->data()) + 1,
            reply->size() - 1);
        return std::nullopt;
    }
    ApprovedSignature sig;
    sig.y_parity = reply->data()[1];
    std::memcpy(sig.r.data(), reply->data() + 2, 32);
    std::memcpy(sig.s.data(), reply->data() + 34, 32);
    return sig;
}

std::optional<KeydClient::Revealed> KeydClient::reveal(
    const SecureBytes& passphrase)
{
    std::vector<uint8_t> frame(1 + passphrase.size());
    frame[0] = uint8_t(Op::Reveal);
    if (!passphrase.empty())
        std::memcpy(frame.data() + 1, passphrase.data(), passphrase.size());
    std::optional<SecureBytes> reply = request(frame.data(), frame.size());
    sodium_memzero(frame.data(), frame.size());
    if (!reply || reply->empty()) {
        m_last_error = "channel broken";
        return std::nullopt;
    }
    if (reply->data()[0] != uint8_t(Op::RootSecret) || reply->size() < 3) {
        m_last_error.assign(reinterpret_cast<const char*>(reply->data()) + 1,
            reply->size() - 1);
        return std::nullopt;
    }
    Revealed out;
    out.kind = RevealKind(reply->data()[1]);
    out.secret = SecureBytes(reply->size() - 2);
    std::memcpy(out.secret.data(), reply->data() + 2, out.secret.size());
    return out;
}

bool KeydClient::deny(uint64_t id)
{
    uint8_t frame[9];
    frame[0] = uint8_t(Op::Deny);
    put_u64le(frame + 1, id);
    std::optional<SecureBytes> reply = request(frame, sizeof frame);
    if (!reply || reply->empty()) {
        m_last_error = "channel broken";
        return false;
    }
    if (reply->data()[0] == uint8_t(Op::Ok))
        return true;
    m_last_error.assign(
        reinterpret_cast<const char*>(reply->data()) + 1, reply->size() - 1);
    return false;
}

std::optional<uint32_t> KeydClient::wait_exit(uint32_t timeout_ms)
{
    return m_child.wait(timeout_ms);
}

void KeydClient::drop_channel()
{
    m_channel.reset();
}

}
