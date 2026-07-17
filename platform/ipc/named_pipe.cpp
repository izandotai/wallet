#include "platform/ipc/named_pipe.hpp"

#include <stdexcept>

#include <windows.h>

#include <sddl.h>

namespace izan::ipc {

namespace {

    std::string current_user_sid()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            throw std::runtime_error("OpenProcessToken failed");
        BYTE buf[256];
        DWORD len = 0;
        const BOOL ok
            = GetTokenInformation(token, TokenUser, buf, sizeof buf, &len);
        CloseHandle(token);
        if (!ok)
            throw std::runtime_error("GetTokenInformation failed");
        LPSTR sidStr = nullptr;
        if (!ConvertSidToStringSidA(
                reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &sidStr))
            throw std::runtime_error("ConvertSidToStringSid failed");
        std::string sid = sidStr;
        LocalFree(sidStr);
        return sid;
    }

    std::string full_name(const std::string& name)
    {
        return "\\\\.\\pipe\\" + name;
    }

}

NamedPipeServer::NamedPipeServer(const std::string& name)
{
    // Owner-only DACL, no inheritance: same-user processes may connect
    // (agents are same-user by definition), everyone else is refused
    // by the object manager before a byte moves.
    const std::string sddl = "D:P(A;;GA;;;" + current_user_sid() + ")";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
        throw std::runtime_error("bad pipe security descriptor");
    SECURITY_ATTRIBUTES sa { sizeof(SECURITY_ATTRIBUTES), sd, FALSE };

    m_pipe = CreateNamedPipeA(full_name(name).c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
            | PIPE_REJECT_REMOTE_CLIENTS,
        1, kMaxMessage, kMaxMessage, 0, &sa);
    LocalFree(sd);
    if (m_pipe == INVALID_HANDLE_VALUE) {
        m_pipe = nullptr;
        // Name already taken = squatting (or a stale instance); the
        // trust plane refuses to start rather than share a name.
        throw std::runtime_error("proposal pipe name unavailable: " + name);
    }
}

NamedPipeServer::~NamedPipeServer()
{
    close();
}

void NamedPipeServer::close()
{
    if (m_pipe) {
        CloseHandle(m_pipe);
        m_pipe = nullptr;
    }
}

bool NamedPipeServer::wait_client()
{
    if (!m_pipe)
        return false;
    if (ConnectNamedPipe(m_pipe, nullptr))
        return true;
    return GetLastError() == ERROR_PIPE_CONNECTED;
}

std::optional<std::vector<uint8_t>> NamedPipeServer::read_message()
{
    std::vector<uint8_t> buf(kMaxMessage);
    DWORD got = 0;
    if (!ReadFile(m_pipe, buf.data(), DWORD(buf.size()), &got, nullptr))
        return std::nullopt;
    buf.resize(got);
    return buf;
}

bool NamedPipeServer::write_message(const uint8_t* data, std::size_t size)
{
    DWORD written = 0;
    return WriteFile(m_pipe, data, DWORD(size), &written, nullptr)
        && written == size;
}

void NamedPipeServer::drop_client()
{
    if (m_pipe) {
        FlushFileBuffers(m_pipe);
        DisconnectNamedPipe(m_pipe);
    }
}

NamedPipeClient::NamedPipeClient(const std::string& name)
{
    // One persistent server instance means a brief busy window while
    // the server turns around between clients; wait it out instead of
    // failing a legitimate caller.
    for (;;) {
        m_pipe
            = CreateFileA(full_name(name).c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_pipe != INVALID_HANDLE_VALUE)
            break;
        m_pipe = nullptr;
        if (GetLastError() != ERROR_PIPE_BUSY
            || !WaitNamedPipeA(full_name(name).c_str(), 5000))
            throw std::runtime_error("cannot reach proposal pipe: " + name);
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr);
}

NamedPipeClient::~NamedPipeClient()
{
    if (m_pipe)
        CloseHandle(m_pipe);
}

std::optional<std::vector<uint8_t>> NamedPipeClient::transact(
    const uint8_t* data, std::size_t size)
{
    std::vector<uint8_t> reply(NamedPipeServer::kMaxMessage);
    DWORD got = 0;
    if (!TransactNamedPipe(m_pipe, const_cast<uint8_t*>(data), DWORD(size),
            reply.data(), DWORD(reply.size()), &got, nullptr))
        return std::nullopt;
    reply.resize(got);
    return reply;
}

}
