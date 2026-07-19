#include "domain/sol/solana.hpp"

#include <stdexcept>
#include <string>

#include <glaze/glaze.hpp>

#include "core/crypto/sol.hpp"

extern "C" {
#include <base58.h>
}

namespace izan::sol {

bool valid_address(std::string_view text)
{
    return crypto::valid_sol_address(text);
}

units::U256 parse_balance_result(std::string_view result_json)
{
    glz::json_t doc;
    if (glz::read_json(doc, result_json) || !doc.is_object())
        throw std::runtime_error("sol: getBalance result not an object");
    const auto& obj = doc.get_object();
    const auto it = obj.find("value");
    if (it == obj.end() || !it->second.is_number())
        throw std::runtime_error("sol: getBalance result missing value");
    const double v = it->second.get_number();
    if (v < 0)
        throw std::runtime_error("sol: negative balance");
    // Lamport totals fit a double exactly up to 2^53 — about nine
    // billion SOL, versus a supply near six hundred million.
    return units::U256::from_u64(uint64_t(v));
}

units::U256 native_balance(chains::RpcClient& rpc, std::string_view address)
{
    if (!valid_address(address))
        throw std::invalid_argument(
            "not a solana address: " + std::string(address));
    return parse_balance_result(
        rpc.call("getBalance", "[\"" + std::string(address) + "\"]"));
}

std::vector<SolSig> parse_signatures(std::string_view result_json)
{
    glz::json_t doc;
    if (glz::read_json(doc, result_json) || !doc.is_array())
        throw std::runtime_error(
            "sol: getSignaturesForAddress result not an array");
    std::vector<SolSig> out;
    for (const glz::json_t& entry : doc.get_array()) {
        if (!entry.is_object())
            continue;
        const auto& obj = entry.get_object();
        const auto sig = obj.find("signature");
        if (sig == obj.end() || !sig->second.is_string())
            continue;
        SolSig rec;
        rec.signature = sig->second.get_string();
        const auto when = obj.find("blockTime");
        if (when != obj.end() && when->second.is_number()
            && when->second.get_number() > 0)
            rec.time = uint64_t(when->second.get_number());
        const auto err = obj.find("err");
        rec.failed = err != obj.end() && !err->second.is_null();
        out.push_back(std::move(rec));
    }
    return out;
}

std::vector<SolSig> recent_signatures(
    chains::RpcClient& rpc, std::string_view address)
{
    if (!valid_address(address))
        throw std::invalid_argument(
            "not a solana address: " + std::string(address));
    return parse_signatures(rpc.call("getSignaturesForAddress",
        "[\"" + std::string(address) + "\",{\"limit\":25}]"));
}

}
