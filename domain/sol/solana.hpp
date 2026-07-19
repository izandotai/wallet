#pragma once

#include <string_view>

#include "core/units/u256.hpp"
#include "domain/chains/rpc_client.hpp"

namespace izan::sol {

// True iff the text decodes as base58 to exactly 32 bytes — a Solana
// account address. Pure; no network.
bool valid_address(std::string_view text);

// The "result" object of a getBalance answer, offline — split out so
// the dialect can be graded without a node.
units::U256 parse_balance_result(std::string_view result_json);

// Native balance in lamports, over the chain's endpoint list (the
// generic JSON-RPC client speaks Solana's dialect unchanged). The
// address is validated before anything touches the wire; malformed
// input throws std::invalid_argument.
units::U256 native_balance(chains::RpcClient& rpc, std::string_view address);

}
