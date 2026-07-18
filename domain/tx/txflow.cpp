#include "domain/tx/txflow.hpp"

#include <stdexcept>
#include <string>

#include <glaze/glaze.hpp>

#include "core/crypto/eth.hpp"

namespace izan::tx {

namespace {

    std::string require_address(std::string_view address)
    {
        std::string checked = crypto::eth_checksum_address(address);
        if (checked.empty())
            throw std::invalid_argument(
                "not an address: " + std::string(address));
        return checked;
    }

    std::string hex_of(std::span<const uint8_t> bytes)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out = "0x";
        out.reserve(2 + bytes.size() * 2);
        for (uint8_t b : bytes) {
            out += digits[b >> 4];
            out += digits[b & 0xf];
        }
        return out;
    }

    // Quantities folded to uint64_t (nonce, gas, block number) exceed
    // it only when a node is lying or broken; refuse rather than
    // truncate into a plausible small number.
    uint64_t quantity_u64(std::string_view hex, const char* what)
    {
        const units::U256 v = units::U256::from_hex(hex);
        uint64_t out = 0;
        for (int i = 0; i < 32; ++i) {
            if (i < 24 && v.be[i])
                throw std::runtime_error(
                    std::string(what) + ": quantity exceeds 64 bits");
            if (i >= 24)
                out = out << 8 | v.be[i];
        }
        return out;
    }

}

uint64_t next_nonce(chains::RpcClient& rpc, std::string_view address)
{
    const std::string addr = require_address(address);
    return quantity_u64(rpc.call_string("eth_getTransactionCount",
                            "[\"" + addr + "\",\"pending\"]"),
        "eth_getTransactionCount");
}

uint64_t estimate_gas(
    chains::RpcClient& rpc, std::string_view from, const Eip1559Tx& draft)
{
    const std::string sender = require_address(from);
    std::string params = "[{\"from\":\"" + sender + "\",\"to\":\""
        + hex_of(draft.to) + "\",\"value\":\"" + draft.value.to_hex() + "\"";
    if (!draft.data.empty())
        params += ",\"data\":\"" + hex_of(draft.data) + "\"";
    params += "}]";
    return quantity_u64(
        rpc.call_string("eth_estimateGas", params), "eth_estimateGas");
}

std::array<uint8_t, 32> broadcast(
    chains::RpcClient& rpc, std::span<const uint8_t> raw)
{
    if (raw.empty())
        throw std::invalid_argument("broadcast: empty transaction");
    const std::array<uint8_t, 32> local = tx_hash(raw);
    const std::string echoed = rpc.call_string(
        "eth_sendRawTransaction", "[\"" + hex_of(raw) + "\"]");
    if (echoed != hex_of(local))
        throw std::runtime_error("broadcast: node echoed hash " + echoed
            + ", raw bytes hash to " + hex_of(local));
    return local;
}

units::U256 parse_base_fee(std::string_view block_json)
{
    glz::generic j;
    if (glz::read_json(j, block_json))
        throw std::runtime_error("block: malformed json");
    if (!j.is_object())
        throw std::runtime_error("block: not an object");
    if (!j.contains("baseFeePerGas") || !j["baseFeePerGas"].is_string())
        throw std::runtime_error("block: no baseFeePerGas (pre-London?)");
    return units::U256::from_hex(j["baseFeePerGas"].get_string());
}

FeeQuote quote_fees(chains::RpcClient& rpc)
{
    FeeQuote q;
    q.base_fee_per_gas = parse_base_fee(
        rpc.call("eth_getBlockByNumber", "[\"latest\",false]"));
    q.max_priority_fee_per_gas = units::U256::from_hex(
        rpc.call_string("eth_maxPriorityFeePerGas", "[]"));
    q.max_fee_per_gas = q.base_fee_per_gas.checked_add(q.base_fee_per_gas)
                            .checked_add(q.max_priority_fee_per_gas);
    return q;
}

std::optional<TxReceipt> parse_receipt(std::string_view result_json)
{
    glz::generic j;
    if (glz::read_json(j, result_json))
        throw std::runtime_error("receipt: malformed json");
    if (j.is_null())
        return std::nullopt;
    if (!j.is_object())
        throw std::runtime_error("receipt: not an object");

    auto field = [&](const char* name) -> std::string {
        if (!j.contains(name) || !j[name].is_string())
            throw std::runtime_error(
                std::string("receipt: missing field ") + name);
        return j[name].get_string();
    };

    TxReceipt r;
    const std::string status = field("status");
    if (status == "0x1")
        r.success = true;
    else if (status == "0x0")
        r.success = false;
    else
        throw std::runtime_error("receipt: unexpected status " + status);
    r.block_number = quantity_u64(field("blockNumber"), "receipt blockNumber");
    r.gas_used = quantity_u64(field("gasUsed"), "receipt gasUsed");
    r.effective_gas_price = units::U256::from_hex(field("effectiveGasPrice"));
    return r;
}

std::optional<TxReceipt> transaction_receipt(
    chains::RpcClient& rpc, std::span<const uint8_t, 32> hash)
{
    return parse_receipt(
        rpc.call("eth_getTransactionReceipt", "[\"" + hex_of(hash) + "\"]"));
}

}
