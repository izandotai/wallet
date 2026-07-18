#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "core/units/u256.hpp"
#include "domain/chains/rpc_client.hpp"
#include "domain/tx/eip1559.hpp"

namespace izan::tx {

// Network half of a send: quote nonce and gas, broadcast the signed
// bytes, poll for the receipt. Addresses are checksum-validated before
// anything touches the wire, same contract as the balance reads.

// Next usable nonce for the sender. Queries the "pending" tag so a
// second send queued behind an unmined first one does not reuse its
// nonce.
uint64_t next_nonce(chains::RpcClient& rpc, std::string_view address);

// Node's gas estimate for a draft. Only to/value/data are read — the
// gas and fee fields are exactly what the caller is still deciding.
// `from` matters: token transfers and contract calls branch on the
// sender's state, and a wrong sender estimates a different execution.
uint64_t estimate_gas(
    chains::RpcClient& rpc, std::string_view from, const Eip1559Tx& draft);

// eth_sendRawTransaction. The returned hash is computed locally from
// the raw bytes and cross-checked against the node's echo — a mismatch
// means the node accepted something other than what was signed, and
// throws instead of handing the caller a hash it cannot trust.
std::array<uint8_t, 32> broadcast(
    chains::RpcClient& rpc, std::span<const uint8_t> raw);

// EIP-1559 fee quote: the node's suggested tip plus a cap with room
// for the base fee to double before the transaction would stall —
// overpayment is refunded by construction, a low cap strands the send.
struct FeeQuote {
    units::U256 max_priority_fee_per_gas;
    units::U256 max_fee_per_gas;
    units::U256 base_fee_per_gas; // latest block, for display
};

FeeQuote quote_fees(chains::RpcClient& rpc);

// Parsing half of quote_fees' block lookup, split out for judging
// without a node: eth_getBlockByNumber's "result" JSON in, its
// baseFeePerGas out. A pre-London chain (no field) throws — type-2
// transactions cannot be priced there.
units::U256 parse_base_fee(std::string_view block_json);

// Receipt of a mined transaction; nullopt while pending or unknown.
struct TxReceipt {
    bool success = false; // status 0x1
    uint64_t block_number = 0;
    uint64_t gas_used = 0;
    units::U256 effective_gas_price;
};

std::optional<TxReceipt> transaction_receipt(
    chains::RpcClient& rpc, std::span<const uint8_t, 32> hash);

// Parsing half of transaction_receipt, split out so the shape checks
// are judgeable without a node: the "result" JSON text goes in, a
// receipt (or nullopt for JSON null) comes out. Malformed shapes throw
// rather than degrade — a receipt drives "did my money move", never
// guess at one.
std::optional<TxReceipt> parse_receipt(std::string_view result_json);

}
