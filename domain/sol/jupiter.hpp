#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace izan::sol {

// Jupiter, Solana's route aggregator: quote → build → we sign. The
// aggregator hands back a fully built versioned transaction; unlike
// our own transfers it cannot be whitelisted byte for byte, so the
// trust story matches the EVM swap lane — verify what CAN be verified
// (the fee payer is us, the shape parses) and present the rest as an
// aggregator-built swap.

struct JupQuote {
    std::string quote_json;       // verbatim — /swap wants it back unchanged
    uint64_t in_amount = 0;
    uint64_t out_amount = 0;      // estimated
    uint64_t out_min = 0;         // slippage floor
    std::string route_label;      // first leg's venue, for the human
    std::string price_impact_pct; // decimal string, as the API speaks
};

JupQuote parse_jup_quote(std::string_view json);
JupQuote jup_quote(std::string_view input_mint, std::string_view output_mint,
    uint64_t amount, uint32_t slippage_bps);

// POST /swap: the built transaction, base64-decoded to wire bytes.
std::vector<uint8_t> parse_jup_swap(std::string_view json);
std::vector<uint8_t> jup_swap_tx(
    const JupQuote& quote, std::string_view user_pubkey);

}
