#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/units/u256.hpp"

namespace izan::codec {

// First four bytes of keccak256 over the canonical signature
// ("balanceOf(address)" — no spaces, no argument names).
std::array<uint8_t, 4> selector(std::string_view signature);

// Calldata for a read call, built word by word. Only what balance and
// token reads need today; dynamic types arrive with the transaction
// engine.
class CallData {
public:
    explicit CallData(std::string_view signature);

    // 20-byte address, left-padded to a word. Accepts 0x-prefixed or
    // bare 40-digit hex; anything else throws.
    CallData& add_address(std::string_view hex);
    CallData& add_u256(const units::U256& value);

    // "0x" + selector + words, lowercase hex.
    std::string to_hex() const;

    // selector + words as raw bytes — transaction calldata (the token
    // transfer engine feeds this straight into Eip1559Tx::data).
    std::vector<uint8_t> to_bytes() const;

private:
    std::array<uint8_t, 4> m_selector;
    std::vector<uint8_t> m_words;
};

// Single-word call result ("0x" + exactly 64 hex digits) as a U256.
// eth_call answers for uint256 getters have precisely this shape;
// anything shorter ("0x" from a non-contract) or longer throws.
units::U256 decode_u256(std::string_view hex_result);

}
