#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/units/u256.hpp"
#include "platform/net/http_client.hpp"

namespace izan::swap {

// LI.FI's native-coin sentinel: the zero address stands for the
// chain's own coin in quote requests.
inline constexpr const char* kNativeToken
    = "0x0000000000000000000000000000000000000000";

// One same-chain swap quote, reduced to what the wallet needs: the
// numbers the human judges by, and the transaction the router wants
// signed — verbatim. The aggregator's own gas price is ignored; our
// fee engine quotes EIP-1559 itself.
struct SwapQuote {
    std::string tool;             // the venue the route runs through
    std::string approval_address; // spender for an ERC-20 sell
    units::U256 from_amount;
    units::U256 to_amount;        // estimated
    units::U256 to_amount_min;    // guaranteed floor
    std::string to;               // the router contract
    std::vector<uint8_t> data;    // calldata, signed as-is
    units::U256 value;            // native amount riding along
    uint64_t gas_limit = 0;       // the router's estimate — a hint
};

// Parse a /v1/quote answer. Throws on anything missing or malformed —
// a swap must never be built from a half-read quote.
SwapQuote parse_quote(std::string_view json);

// The request target (path + query) for a same-chain quote. Amounts
// are base units; the integrator string rides every request.
std::string quote_target(uint64_t chain_id, std::string_view from_token,
    std::string_view to_token, const units::U256& amount,
    std::string_view from_address, std::string_view integrator);

// One round trip to li.quest. Throws on transport or parse failure.
SwapQuote fetch_quote(net::HttpsClient& client, uint64_t chain_id,
    std::string_view from_token, std::string_view to_token,
    const units::U256& amount, std::string_view from_address,
    std::string_view integrator);

}
