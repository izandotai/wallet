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

}
