#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace izan::units {

// 256-bit unsigned integer as 32 big-endian bytes. Token amounts, wei
// balances and ABI words all live in this shape; big-endian keeps it
// byte-compatible with ABI encoding and hash inputs, and makes
// comparison plain lexicographic order.
//
// Arithmetic is deliberately minimal: checked add/sub for totals.
// Amounts are money — silent wraparound is not an acceptable failure
// mode, so overflow throws.
struct U256 {
    std::array<uint8_t, 32> be {};

    static U256 from_u64(uint64_t v)
    {
        U256 r;
        for (int i = 0; i < 8; ++i)
            r.be[31 - i] = uint8_t(v >> (8 * i));
        return r;
    }

    static U256 from_dec(std::string_view dec)
    {
        if (dec.empty())
            throw std::invalid_argument("U256::from_dec: empty");
        U256 r;
        for (char c : dec) {
            if (c < '0' || c > '9')
                throw std::invalid_argument("U256::from_dec: bad char");
            uint32_t carry = uint32_t(c - '0');
            for (int i = 31; i >= 0; --i) {
                uint32_t v = uint32_t(r.be[i]) * 10 + carry;
                r.be[i] = uint8_t(v & 0xff);
                carry = v >> 8;
            }
            if (carry)
                throw std::overflow_error("U256::from_dec: overflow");
        }
        return r;
    }

    // Ethereum JSON-RPC quantity: "0x" + minimal hex, odd nibble counts
    // included ("0x1a", "0x0"). Bare hex without the prefix also parses.
    static U256 from_hex(std::string_view hex)
    {
        if (hex.starts_with("0x") || hex.starts_with("0X"))
            hex.remove_prefix(2);
        if (hex.empty() || hex.size() > 64)
            throw std::invalid_argument("U256::from_hex: bad length");
        U256 r;
        std::size_t out = 32;
        for (std::size_t i = hex.size(); i >= 2; i -= 2)
            r.be[--out] = uint8_t(nibble(hex[i - 2]) << 4 | nibble(hex[i - 1]));
        if (hex.size() % 2)
            r.be[--out] = nibble(hex[0]);
        return r;
    }

    std::string to_dec() const
    {
        std::array<uint8_t, 32> tmp = be;
        std::string out;
        while (true) {
            uint32_t rem = 0;
            bool any = false;
            for (auto& b : tmp) {
                uint32_t cur = (rem << 8) | b;
                b = uint8_t(cur / 10);
                rem = cur % 10;
                any = any || b;
            }
            out.push_back(char('0' + rem));
            if (!any)
                break;
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    // Minimal JSON-RPC quantity form ("0x0", "0x1a").
    std::string to_hex() const
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out = "0x";
        bool started = false;
        for (uint8_t b : be) {
            if (!started && !b)
                continue;
            if (started || (b >> 4))
                out += digits[b >> 4];
            out += digits[b & 0xf];
            started = true;
        }
        if (!started)
            out += '0';
        return out;
    }

    bool is_zero() const
    {
        for (uint8_t b : be)
            if (b)
                return false;
        return true;
    }

    U256 checked_add(const U256& other) const
    {
        U256 r;
        uint32_t carry = 0;
        for (int i = 31; i >= 0; --i) {
            uint32_t v = uint32_t(be[i]) + other.be[i] + carry;
            r.be[i] = uint8_t(v & 0xff);
            carry = v >> 8;
        }
        if (carry)
            throw std::overflow_error("U256: add overflow");
        return r;
    }

    U256 checked_sub(const U256& other) const
    {
        U256 r;
        int32_t borrow = 0;
        for (int i = 31; i >= 0; --i) {
            int32_t v = int32_t(be[i]) - other.be[i] - borrow;
            borrow = v < 0;
            r.be[i] = uint8_t(v + (borrow << 8));
        }
        if (borrow)
            throw std::underflow_error("U256: sub underflow");
        return r;
    }

    friend auto operator<=>(const U256&, const U256&) = default;

private:
    static uint8_t nibble(char c)
    {
        if (c >= '0' && c <= '9')
            return uint8_t(c - '0');
        if (c >= 'a' && c <= 'f')
            return uint8_t(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
            return uint8_t(c - 'A' + 10);
        throw std::invalid_argument("U256::from_hex: bad char");
    }
};

}
