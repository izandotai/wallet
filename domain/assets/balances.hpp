#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/units/u256.hpp"
#include "domain/chains/rpc_client.hpp"

namespace izan::assets {

// Balance reads for watch-only mode. Addresses are validated (EIP-55
// checksum machinery, any input case) before anything touches the
// network; malformed input throws std::invalid_argument.

// Native coin balance in base units (wei and friends).
units::U256 native_balance(chains::RpcClient& rpc, std::string_view address);

// ERC-20 balanceOf(holder) on the token contract, in the token's base
// units. A non-contract token address surfaces as a decode error.
units::U256 erc20_balance(
    chains::RpcClient& rpc, std::string_view token, std::string_view holder);

// A token's self-description, asked of the contract itself — the add
// form must never trust a hand-typed symbol or decimals. An empty
// symbol or decimals beyond the display range throw: whatever
// answered is not a token this wallet can show honestly.
struct TokenProbe {
    std::string symbol;
    uint8_t decimals {};
};

TokenProbe probe_token(chains::RpcClient& rpc, std::string_view token);

// ERC-20 allowance(owner, spender) — how much the spender may already
// move; the swap flow asks before proposing an approval.
units::U256 erc20_allowance(chains::RpcClient& rpc, std::string_view token,
    std::string_view owner, std::string_view spender);

// approve(spender, amount) calldata — the transaction engine feeds it
// straight into Eip1559Tx::data. Exact-amount approvals only; this
// wallet never asks anyone to sign an unlimited one.
std::vector<uint8_t> erc20_approve_calldata(
    std::string_view spender, const units::U256& amount);

}
