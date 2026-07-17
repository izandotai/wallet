#include "domain/chains/chain_spec.hpp"

#include <stdexcept>
#include <unordered_set>

#include <glaze/glaze.hpp>

namespace izan::chains {

ChainRegistry ChainRegistry::from_json(std::string_view json)
{
    ChainRegistry reg;
    if (glz::read<glz::opts { .error_on_unknown_keys = false }>(
            reg.m_chains, json))
        throw std::runtime_error("chains config: not a valid chain array");

    std::unordered_set<uint64_t> seen;
    for (const ChainSpec& c : reg.m_chains) {
        if (!c.chain_id)
            throw std::runtime_error("chains config: chain_id 0 or missing");
        if (!seen.insert(c.chain_id).second)
            throw std::runtime_error("chains config: duplicate chain_id "
                + std::to_string(c.chain_id));
        if (c.name.empty() || c.symbol.empty())
            throw std::runtime_error("chains config: chain "
                + std::to_string(c.chain_id) + " missing name or symbol");
        if (c.rpc.empty())
            throw std::runtime_error("chains config: chain "
                + std::to_string(c.chain_id) + " has no rpc endpoints");
        for (const std::string& url : c.rpc)
            if (!url.starts_with("https://") && !url.starts_with("http://"))
                throw std::runtime_error("chains config: bad rpc url " + url);
    }
    return reg;
}

const ChainSpec* ChainRegistry::by_id(uint64_t chain_id) const
{
    for (const ChainSpec& c : m_chains)
        if (c.chain_id == chain_id)
            return &c;
    return nullptr;
}

}
