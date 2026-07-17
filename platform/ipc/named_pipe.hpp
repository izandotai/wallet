#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace izan::ipc {

// The proposal endpoint. Named pipes are connectable by other
// processes — that is their job here (agents and scripts must find the
// door) — so the defenses are: a DACL granting access to the current
// user only, FIRST_PIPE_INSTANCE so a squatter who pre-created the
// name makes us fail loudly instead of the client talking to an
// impostor, remote clients rejected, and one persistent instance so
// there is never a window for a second instance between connections.
// No secret ever travels this channel.
class NamedPipeServer {
public:
    explicit NamedPipeServer(const std::string& name);
    ~NamedPipeServer();

    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;

    // Blocks for the next client connection. false = server closed.
    bool wait_client();
    // One message-mode datagram. nullopt = client went away.
    std::optional<std::vector<uint8_t>> read_message();
    bool write_message(const uint8_t* data, std::size_t size);
    void drop_client();
    void close();

    static constexpr std::size_t kMaxMessage = 64 * 1024;

private:
    void* m_pipe = nullptr;
};

// Client side, used by the UI process and by tests standing in for an
// external agent.
class NamedPipeClient {
public:
    explicit NamedPipeClient(const std::string& name);
    ~NamedPipeClient();

    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;

    std::optional<std::vector<uint8_t>> transact(
        const uint8_t* data, std::size_t size);

private:
    void* m_pipe = nullptr;
};

}
