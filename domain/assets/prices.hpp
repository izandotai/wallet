#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace izan::assets {

// USD quotes for the portfolio, from the CoinGecko public price
// endpoint. Prices are garnish over the on-chain numbers: a failed
// fetch leaves the fiat column empty, never the balances.

// Maps an asset symbol to its CoinGecko id; empty when the symbol is
// not in the table — an unknown asset shows no fiat line, because a
// wrong number is worse than none.
std::string coingecko_id(std::string_view symbol);

// Parses a simple/price response body ({"ethereum":{"usd":3500.1},…})
// into id → USD price. Malformed bodies throw; entries without a usd
// figure are skipped.
std::unordered_map<std::string, double> parse_usd_prices(std::string_view json);

// One round trip for every id at once. Throws on transport or parse
// failure.
std::unordered_map<std::string, double> fetch_usd_prices(
    const std::vector<std::string>& ids);

}
