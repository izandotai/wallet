#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace izan::sol {

// Solana legacy transaction encoding, transfers only — the exact
// bytes keyd signs and the approval screen decodes back. Encoder and
// parser are inverses in this TU; nothing here touches the network.

// Solana's three-byte variable length ("compact-u16", ULEB128 capped
// at u16). Exposed for its own test vectors.
void put_compact_u16(std::vector<uint8_t>& out, uint16_t v);
uint16_t take_compact_u16(std::span<const uint8_t> in, std::size_t& pos);

// The System.Transfer legacy message: header 1/0/1, accounts
// [from, to, system program], the blockhash, one instruction with
// u32le(2) || u64le(lamports). Throws on a bad address, from == to,
// or zero lamports — garbage never leaves the domain layer.
std::vector<uint8_t> encode_transfer_message(std::string_view from,
    std::string_view to, uint64_t lamports,
    std::span<const uint8_t, 32> blockhash);

struct SolTransfer {
    std::string from;
    std::string to;
    uint64_t lamports = 0;
    std::array<uint8_t, 32> blockhash {};
};

// The inverse: accepts EXACTLY the shape the encoder emits and throws
// on anything else — extra instructions, foreign programs, stowaway
// accounts. The approval screen's whitelist is this strictness.
SolTransfer parse_transfer_message(std::span<const uint8_t> message);

// signature || message, ready for base64 and sendTransaction.
std::vector<uint8_t> assemble_tx(
    std::span<const uint8_t, 64> sig, std::span<const uint8_t> message);

}
