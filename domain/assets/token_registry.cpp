#include "domain/assets/token_registry.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

#include <glaze/glaze.hpp>

#include "core/crypto/eth.hpp"
#include "core/units/decimal.hpp"

namespace izan::assets {

TokenRegistry TokenRegistry::from_json(std::string_view json)
{
    TokenRegistry reg;
    if (glz::read<glz::opts { .error_on_unknown_keys = false }>(
            reg.m_tokens, json))
        throw std::runtime_error("tokens config: not a valid token array");

    std::set<std::pair<uint64_t, std::string>> seen;
    for (TokenSpec& t : reg.m_tokens) {
        if (!t.chain_id)
            throw std::runtime_error("tokens config: chain_id 0 or missing");
        std::string checked = crypto::eth_checksum_address(t.address);
        if (checked.empty())
            throw std::runtime_error("tokens config: bad address " + t.address);
        t.address = std::move(checked);
        if (t.symbol.empty())
            throw std::runtime_error(
                "tokens config: " + t.address + " missing symbol");
        if (t.decimals > units::kMaxDecimals)
            throw std::runtime_error(
                "tokens config: " + t.address + " decimals out of range");
        std::string lower = t.address;
        std::ranges::transform(lower, lower.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        if (!seen.insert({ t.chain_id, lower }).second)
            throw std::runtime_error("tokens config: duplicate " + t.address
                + " on chain " + std::to_string(t.chain_id));
    }
    return reg;
}

std::vector<const TokenSpec*> TokenRegistry::tokens_for(uint64_t chain_id) const
{
    std::vector<const TokenSpec*> out;
    for (const TokenSpec& t : m_tokens)
        if (t.chain_id == chain_id)
            out.push_back(&t);
    return out;
}

}
