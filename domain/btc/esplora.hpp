#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/units/u256.hpp"
#include "domain/chains/chain_spec.hpp"

namespace izan::btc {

// True iff the text is a Bitcoin mainnet address this wallet can
// reason about: base58check with version 0x00 (P2PKH) or 0x05 (P2SH),
// or bech32/bech32m under the "bc" prefix (segwit v0/v1). Pure; no
// network.
bool valid_address(std::string_view text);

// The confirmed balance out of an esplora /address/{addr} answer —
// funded minus spent, chain stats only; unconfirmed money is not
// money yet. Split out so the dialect grades offline.
units::U256 parse_address_stats(std::string_view json);

// Confirmed balance in satoshi over the chain's esplora endpoints,
// first answer wins. The address is validated before anything touches
// the wire; malformed input throws std::invalid_argument.
units::U256 native_balance(
    const chains::ChainSpec& spec, std::string_view address);

// One page of an address's transactions, from our side of the ledger:
// net = received into the address minus spent out of it, direction by
// its sign, the counterparty the first foreign address on the other
// side. Unconfirmed entries are skipped — not money yet, and no
// timestamp to sort by.
struct BtcTx {
    std::string txid;
    uint64_t time = 0; // block time, unix seconds
    bool incoming = false;
    units::U256 amount; // |net| in satoshi
    std::string counterparty;
};

std::vector<BtcTx> parse_txs(std::string_view json, std::string_view self);

// The 25 most recent transactions, esplora's /address/{addr}/txs.
std::vector<BtcTx> fetch_txs(
    const chains::ChainSpec& spec, std::string_view address);

}
