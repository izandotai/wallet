#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/units/u256.hpp"
#include "domain/assets/token_registry.hpp"
#include "domain/chains/chain_spec.hpp"
#include "domain/chains/rpc_client.hpp"

namespace izan::assets {

// One row of a watch-only snapshot: a native coin or token position on
// one chain. A failed read stays a row — a chain being down must look
// like "unreadable", never like "empty".
struct Holding {
    uint64_t chain_id {};
    std::string chain; // display name
    std::string symbol;
    std::string token; // contract address, empty = native coin
    uint8_t decimals {};
    units::U256 amount;
    bool ok = false;
    std::string error; // set when !ok
};

// Reads native + configured token balances for an address across every
// chain in the registry. RPC connections are kept per chain and reused
// between snapshots.
class PortfolioReader {
public:
    // Cross-validates the pair: a token on a chain the registry does
    // not know is a config typo and throws here, loudly, instead of
    // silently never showing up.
    PortfolioReader(chains::ChainRegistry chains, TokenRegistry tokens);

    // Throws on a malformed address; per-chain and per-token read
    // failures are reported inside the rows.
    std::vector<Holding> snapshot(std::string_view address);

private:
    chains::ChainRegistry m_chains;
    TokenRegistry m_tokens;
    std::unordered_map<uint64_t, std::unique_ptr<chains::RpcClient>> m_clients;
};

}
