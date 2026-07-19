#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "domain/chains/chain_spec.hpp"

namespace izan::btc {

// One spendable coin, as esplora lists it.
struct Utxo {
    std::string txid;
    uint32_t vout = 0;
    uint64_t value = 0; // satoshi
};

// Parse /address/{addr}/utxo — confirmed entries only, same money
// discipline as the balance road.
std::vector<Utxo> parse_utxos(std::string_view json);
std::vector<Utxo> fetch_utxos(
    const chains::ChainSpec& spec, std::string_view address);

// What the selector decided: which coins to spend, the fee those
// coins imply at the asked rate, and the change coming home. change
// below the dust bound folds into the fee (an output nobody could
// ever afford to spend is a gift to miners either way).
struct CoinSelection {
    std::vector<Utxo> inputs;
    uint64_t fee = 0;
    uint64_t change = 0; // 0 = single-output transaction
};

inline constexpr uint64_t kDustSats = 546;

// Virtual size of a P2WPKH-input transaction, slightly conservative
// (fractional weight units round up per part).
uint64_t p2wpkh_vsize(std::size_t inputs, std::size_t outputs);

// Greedy by value, descending, iterated to a fixed point: each added
// input raises the fee, which may demand another input. Deterministic
// for a given list. Throws when the coins cannot cover amount + fee.
CoinSelection select_coins(
    std::vector<Utxo> utxos, uint64_t amount, uint64_t feerate_sat_vb);

// MAX: every coin in, no change; returns the selection whose
// amount-to-recipient is (total - fee). Throws when fee eats it all.
CoinSelection sweep_coins(std::vector<Utxo> utxos, uint64_t feerate_sat_vb);

}
