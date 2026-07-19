#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/units/u256.hpp"
#include "domain/chains/chain_spec.hpp"

namespace izan::assets {

// One line of an address's ledger — a native-coin movement from the
// txlist endpoint, or an ERC-20 transfer from tokentx (which then
// carries the token's own symbol and decimals).
struct TxRecord {
    std::string hash;
    std::string counterparty;    // the other side of the movement
    units::U256 value;
    uint64_t time = 0;           // unix seconds
    bool incoming = false;
    bool failed = false;
    std::string token_symbol;    // empty = the chain's native coin
    unsigned token_decimals = 0; // meaningful only with a symbol
};

// Parses an etherscan-style txlist answer ({"status","result":[…]})
// into records, judged from `self`'s side of each row (case blind —
// explorers answer in lowercase). "No transactions found" is an empty
// vector, not an error; a malformed body throws.
std::vector<TxRecord> parse_txlist(
    std::string_view json, std::string_view self);

// Same grammar for the tokentx endpoint: ERC-20 transfer events, each
// carrying tokenSymbol and tokenDecimal. Rows whose token identity or
// value cannot be read are dropped, not invented.
std::vector<TxRecord> parse_tokentx(
    std::string_view json, std::string_view self);

// One page of history (newest first, 25 per page, page counts from 1)
// from the chain's configured Blockscout instance. A chain without a
// history base answers empty. Throws on transport or parse failure.
std::vector<TxRecord> fetch_history(
    const chains::ChainSpec& chain, const std::string& address, int page = 1);

// One page of ERC-20 transfers for the same address.
std::vector<TxRecord> fetch_token_history(
    const chains::ChainSpec& chain, const std::string& address, int page = 1);

// Both endpoints' page N over one keep-alive connection — they share
// a host, and the callers always want the pair.
struct Ledger {
    std::vector<TxRecord> native;
    std::vector<TxRecord> tokens;
};

Ledger fetch_ledger(
    const chains::ChainSpec& chain, const std::string& address, int page = 1);

}
