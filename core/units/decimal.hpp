#pragma once

#include <string>
#include <string_view>

#include "core/units/u256.hpp"

namespace izan::units {

// Base-unit amount ↔ human decimal string, exact in both directions.
// No floating point anywhere near money: 0.1 has no binary
// representation and a wallet that displays rounding artifacts is a
// wallet nobody trusts.

// 10^77 still fits in a U256; more fractional digits than that cannot.
inline constexpr unsigned kMaxDecimals = 77;

// "1500000000000000000", 18 → "1.5"; trailing zeros trimmed, integers
// come out bare ("1", not "1.0").
inline std::string format_units(const U256& amount, unsigned decimals)
{
    if (decimals > kMaxDecimals)
        throw std::invalid_argument("format_units: decimals out of range");
    std::string dec = amount.to_dec();
    if (!decimals)
        return dec;
    if (dec.size() <= decimals)
        dec.insert(0, decimals + 1 - dec.size(), '0');
    std::string whole = dec.substr(0, dec.size() - decimals);
    std::string frac = dec.substr(dec.size() - decimals);
    while (!frac.empty() && frac.back() == '0')
        frac.pop_back();
    if (frac.empty())
        return whole;
    return whole + "." + frac;
}

// "1.5", 18 → 1500000000000000000. Strict: digits and at most one dot,
// a digit on each side of it, no more fractional digits than the token
// carries (never silently round user input), no signs, no whitespace.
inline U256 parse_units(std::string_view text, unsigned decimals)
{
    if (decimals > kMaxDecimals)
        throw std::invalid_argument("parse_units: decimals out of range");
    const std::size_t dot = text.find('.');
    std::string_view whole = text.substr(0, dot);
    std::string_view frac
        = dot == std::string_view::npos ? "" : text.substr(dot + 1);
    if (whole.empty() || (dot != std::string_view::npos && frac.empty()))
        throw std::invalid_argument("parse_units: malformed amount");
    if (frac.size() > decimals)
        throw std::invalid_argument("parse_units: too many decimal places");
    std::string digits;
    digits.reserve(whole.size() + decimals);
    digits.append(whole);
    digits.append(frac);
    digits.append(decimals - frac.size(), '0');
    return U256::from_dec(digits); // rejects non-digits (a second dot too)
}

}
