#include "core/codec/abi.hpp"

#include <stdexcept>

#include <sha3.h>

namespace izan::codec {

namespace {

    uint8_t nibble(char c)
    {
        if (c >= '0' && c <= '9')
            return uint8_t(c - '0');
        if (c >= 'a' && c <= 'f')
            return uint8_t(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
            return uint8_t(c - 'A' + 10);
        throw std::invalid_argument("abi: bad hex digit");
    }

}

std::array<uint8_t, 4> selector(std::string_view signature)
{
    uint8_t digest[32];
    keccak_256(reinterpret_cast<const uint8_t*>(signature.data()),
        signature.size(), digest);
    return { digest[0], digest[1], digest[2], digest[3] };
}

CallData::CallData(std::string_view signature)
    : m_selector(selector(signature))
{
}

CallData& CallData::add_address(std::string_view hex)
{
    if (hex.starts_with("0x") || hex.starts_with("0X"))
        hex.remove_prefix(2);
    if (hex.size() != 40)
        throw std::invalid_argument("abi: address must be 40 hex digits");
    m_words.insert(m_words.end(), 12, 0);
    for (std::size_t i = 0; i < 40; i += 2)
        m_words.push_back(uint8_t(nibble(hex[i]) << 4 | nibble(hex[i + 1])));
    return *this;
}

CallData& CallData::add_u256(const units::U256& value)
{
    m_words.insert(m_words.end(), value.be.begin(), value.be.end());
    return *this;
}

std::string CallData::to_hex() const
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "0x";
    out.reserve(2 + 8 + m_words.size() * 2);
    auto emit = [&](uint8_t b) {
        out += digits[b >> 4];
        out += digits[b & 0xf];
    };
    for (uint8_t b : m_selector)
        emit(b);
    for (uint8_t b : m_words)
        emit(b);
    return out;
}

std::vector<uint8_t> CallData::to_bytes() const
{
    std::vector<uint8_t> out;
    out.reserve(m_selector.size() + m_words.size());
    out.insert(out.end(), m_selector.begin(), m_selector.end());
    out.insert(out.end(), m_words.begin(), m_words.end());
    return out;
}

units::U256 decode_u256(std::string_view hex_result)
{
    if (hex_result.starts_with("0x") || hex_result.starts_with("0X"))
        hex_result.remove_prefix(2);
    if (hex_result.size() != 64)
        throw std::invalid_argument("abi: expected one 32-byte word, got "
            + std::to_string(hex_result.size()) + " hex digits");
    return units::U256::from_hex(hex_result);
}

}
