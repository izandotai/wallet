#include "domain/sol/sol_tx.hpp"

#include <cstring>
#include <stdexcept>

#include "core/crypto/sol.hpp"

extern "C" {
#include <base58.h>
}

namespace izan::sol {

namespace {

    std::array<uint8_t, 32> decode_address(std::string_view text)
    {
        std::array<uint8_t, 32> out {};
        // b58tobin writes right-aligned into the buffer it is given;
        // a Solana account is exactly 32 bytes, no more, no fewer.
        std::size_t sz = out.size();
        if (!b58tobin(out.data(), &sz, std::string(text).c_str())
            || sz != out.size())
            throw std::invalid_argument(
                "sol-tx: not a solana address: " + std::string(text));
        return out;
    }

    void put_u32le(std::vector<uint8_t>& out, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t(v >> (8 * i)));
    }

    void put_u64le(std::vector<uint8_t>& out, uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            out.push_back(uint8_t(v >> (8 * i)));
    }

}

void put_compact_u16(std::vector<uint8_t>& out, uint16_t v)
{
    uint32_t rem = v;
    while (true) {
        const uint8_t byte = rem & 0x7f;
        rem >>= 7;
        if (rem == 0) {
            out.push_back(byte);
            return;
        }
        out.push_back(byte | 0x80);
    }
}

uint16_t take_compact_u16(std::span<const uint8_t> in, std::size_t& pos)
{
    uint32_t v = 0;
    for (int shift = 0; shift < 21; shift += 7) {
        if (pos >= in.size())
            throw std::runtime_error("sol-tx: compact-u16 runs off the end");
        const uint8_t byte = in[pos++];
        v |= uint32_t(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            if (v > 0xffff)
                throw std::runtime_error("sol-tx: compact-u16 overflow");
            return uint16_t(v);
        }
    }
    throw std::runtime_error("sol-tx: compact-u16 overlong");
}

std::vector<uint8_t> encode_transfer_message(std::string_view from,
    std::string_view to, uint64_t lamports,
    std::span<const uint8_t, 32> blockhash)
{
    if (lamports == 0)
        throw std::invalid_argument("sol-tx: zero lamports");
    const auto from_pk = decode_address(from);
    const auto to_pk = decode_address(to);
    // A self-transfer is legal money (the cheapest real-coin test
    // there is), but an account may sit on the key table only once —
    // so it wears the two-key shape, the instruction naming the same
    // index twice.
    const bool self = from == to;
    std::vector<uint8_t> out;
    // Header: 1 signature required, 0 read-only signed, 1 read-only
    // unsigned (the system program).
    out.push_back(1);
    out.push_back(0);
    out.push_back(1);
    put_compact_u16(out, self ? 2 : 3);
    out.insert(out.end(), from_pk.begin(), from_pk.end());
    if (!self)
        out.insert(out.end(), to_pk.begin(), to_pk.end());
    out.insert(out.end(), 32, 0); // system program: 32 zero bytes
    out.insert(out.end(), blockhash.begin(), blockhash.end());
    put_compact_u16(out, 1);      // one instruction
    out.push_back(self ? 1 : 2);  // program index into the key table
    put_compact_u16(out, 2);      // two account references
    out.push_back(0);
    out.push_back(self ? 0 : 1);
    put_compact_u16(out, 12);     // 4 tag + 8 lamports
    put_u32le(out, 2);            // SystemInstruction::Transfer
    put_u64le(out, lamports);
    return out;
}

SolTransfer parse_transfer_message(std::span<const uint8_t> message)
{
    std::size_t pos = 0;
    auto take = [&](std::size_t n) -> const uint8_t* {
        if (pos + n > message.size())
            throw std::runtime_error("sol-tx: message truncated");
        const uint8_t* p = message.data() + pos;
        pos += n;
        return p;
    };
    const uint8_t* header = take(3);
    if (header[0] != 1 || header[1] != 0 || header[2] != 1)
        throw std::runtime_error("sol-tx: not a single-signer transfer");
    const uint16_t keys = take_compact_u16(message, pos);
    if (keys != 2 && keys != 3)
        throw std::runtime_error("sol-tx: stowaway accounts");
    const bool self = keys == 2; // the self-transfer shape
    SolTransfer out;
    const uint8_t* from_pk = take(32);
    const uint8_t* to_pk = self ? from_pk : take(32);
    const uint8_t* program = take(32);
    for (int i = 0; i < 32; ++i)
        if (program[i] != 0)
            throw std::runtime_error("sol-tx: not the system program");
    const uint8_t* hash = take(32);
    std::memcpy(out.blockhash.data(), hash, 32);
    if (take_compact_u16(message, pos) != 1)
        throw std::runtime_error("sol-tx: expected exactly one instruction");
    if (*take(1) != (self ? 1 : 2))
        throw std::runtime_error("sol-tx: instruction on a foreign program");
    if (take_compact_u16(message, pos) != 2)
        throw std::runtime_error("sol-tx: wrong account count");
    const uint8_t* accts = take(2);
    if (accts[0] != 0 || accts[1] != (self ? 0 : 1))
        throw std::runtime_error("sol-tx: wrong account order");
    if (take_compact_u16(message, pos) != 12)
        throw std::runtime_error("sol-tx: wrong data length");
    const uint8_t* data = take(12);
    uint32_t tag = 0;
    for (int i = 0; i < 4; ++i)
        tag |= uint32_t(data[i]) << (8 * i);
    if (tag != 2)
        throw std::runtime_error("sol-tx: not a transfer instruction");
    for (int i = 0; i < 8; ++i)
        out.lamports |= uint64_t(data[4 + i]) << (8 * i);
    if (out.lamports == 0)
        throw std::runtime_error("sol-tx: zero lamports");
    if (pos != message.size())
        throw std::runtime_error("sol-tx: trailing bytes");
    out.from = crypto::sol_address(std::span<const uint8_t, 32>(from_pk, 32));
    out.to = crypto::sol_address(std::span<const uint8_t, 32>(to_pk, 32));
    return out;
}

std::vector<uint8_t> assemble_tx(
    std::span<const uint8_t, 64> sig, std::span<const uint8_t> message)
{
    std::vector<uint8_t> out;
    put_compact_u16(out, 1);
    out.insert(out.end(), sig.begin(), sig.end());
    out.insert(out.end(), message.begin(), message.end());
    return out;
}

}
