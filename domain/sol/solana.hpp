#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// One line of an account's activity feed. Solana's cheap index stops
// at the signature — knowing what moved would cost a full-transaction
// fetch per row — so the feed shows involvement, not amounts.
struct SolSig {
    std::string signature;
    uint64_t time = 0; // blockTime, unix seconds; 0 when the node lost it
    bool failed = false;
};

// The "result" array of getSignaturesForAddress, offline.
std::vector<SolSig> parse_signatures(std::string_view result_json);

// The 25 most recent signatures touching the address, newest first.
std::vector<SolSig> recent_signatures(
    chains::RpcClient& rpc, std::string_view address);

// ---- The send flow's network legs; each parse_* grades offline. ----

// getLatestBlockhash → the 32 bytes a transfer message carries.
std::array<uint8_t, 32> parse_blockhash_result(std::string_view result_json);
std::array<uint8_t, 32> latest_blockhash(chains::RpcClient& rpc);

// sendTransaction over base64; the node answers with the signature it
// filed the transaction under.
std::string send_transaction(
    chains::RpcClient& rpc, std::span<const uint8_t> tx);

// getSignatureStatuses, one signature deep.
enum class SigStatus : uint8_t {
    Unknown, // the node has not seen it (yet)
    Processed,
    Confirmed,
    Finalized,
    Failed, // landed with an error — on-chain and lost
};
SigStatus parse_signature_status(std::string_view result_json);
SigStatus signature_status(chains::RpcClient& rpc, std::string_view signature);

// getMinimumBalanceForRentExemption: the lamport floor below which an
// account of that byte size cannot exist. 0 = a bare wallet (the
// send guard), 165 = an SPL token account (the ATA-opening rent).
uint64_t rent_exempt_minimum(chains::RpcClient& rpc, uint64_t size = 0);

// getAccountInfo, existence only — a null value is an unopened door.
bool account_exists(chains::RpcClient& rpc, std::string_view address);

// The decimals a mint declares on-chain, via getTokenSupply — the
// only authority a strange token has.
uint8_t mint_decimals(chains::RpcClient& rpc, std::string_view mint);

// ---- SPL tokens: the holdings under the token program ----

struct SplHolding {
    std::string account; // the token account (usually the ATA)
    std::string mint;
    uint64_t amount = 0; // base units
    uint8_t decimals = 0;
};

// Parse a getTokenAccountsByOwner (jsonParsed) result. Zero-balance
// accounts are kept — an empty ATA is still an account the owner may
// want to see; callers filter.
std::vector<SplHolding> parse_token_accounts(std::string_view result_json);
std::vector<SplHolding> token_accounts(
    chains::RpcClient& rpc, std::string_view owner);

// The display name a well-known mint answers to; empty for strangers.
// A tiny curated table, not a registry — pricing and symbols for the
// long tail come later, and an unknown mint shows its own address.
std::string known_mint_symbol(std::string_view mint);

}
