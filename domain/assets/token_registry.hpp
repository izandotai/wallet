#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace izan::assets {

// One ERC-20 the wallet watches. Tokens are config, not code, same as
// chains.
struct TokenSpec {
    uint64_t chain_id {};
    std::string address; // stored checksummed
    std::string symbol;
    uint8_t decimals {};
};

class TokenRegistry {
public:
    // Parses a JSON array and validates it as a whole: addresses must
    // checksum-validate (and are normalized to EIP-55 form), symbols
    // present, decimals within U256 range, no duplicate
    // (chain, address) pairs. Throws on any violation.
    static TokenRegistry from_json(std::string_view json);

    std::vector<const TokenSpec*> tokens_for(uint64_t chain_id) const;

    const std::vector<TokenSpec>& all() const
    {
        return m_tokens;
    }

private:
    std::vector<TokenSpec> m_tokens;
};

}
