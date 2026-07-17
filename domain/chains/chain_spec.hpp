#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace izan::chains {

// One EVM network. Adding a chain is a config entry, not a code change;
// non-EVM families (BTC, Solana) arrive later behind IChainFamily.
struct ChainSpec {
    uint64_t chain_id {};
    std::string name;
    std::string symbol;           // native currency ticker
    uint8_t decimals = 18;        // native currency decimals
    std::vector<std::string> rpc; // endpoints in priority order
    std::string explorer;         // base URL, empty = none
};

class ChainRegistry {
public:
    // Parses a JSON array of chain specs and validates it as a whole:
    // ids unique and nonzero, name/symbol present, at least one rpc
    // endpoint per chain, endpoints http(s) only. Throws on any
    // violation — a wallet must not limp along on a half-read config.
    static ChainRegistry from_json(std::string_view json);

    const ChainSpec* by_id(uint64_t chain_id) const;

    const std::vector<ChainSpec>& all() const
    {
        return m_chains;
    }

private:
    std::vector<ChainSpec> m_chains;
};

}
