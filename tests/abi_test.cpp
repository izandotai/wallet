#include <doctest/doctest.h>

#include <string>

#include "core/codec/abi.hpp"

using izan::codec::CallData;
using izan::codec::decode_u256;
using izan::codec::selector;
using izan::units::U256;

namespace {

std::string hex4(const std::array<uint8_t, 4>& b)
{
    std::string out;
    static constexpr char digits[] = "0123456789abcdef";
    for (uint8_t x : b) {
        out += digits[x >> 4];
        out += digits[x & 0xf];
    }
    return out;
}

}

TEST_CASE("selectors match the public constants")
{
    // These four are burned into every EVM explorer and dataset.
    CHECK(hex4(selector("transfer(address,uint256)")) == "a9059cbb");
    CHECK(hex4(selector("balanceOf(address)")) == "70a08231");
    CHECK(hex4(selector("totalSupply()")) == "18160ddd");
    CHECK(hex4(selector("approve(address,uint256)")) == "095ea7b3");
}

TEST_CASE("calldata is selector plus left-padded words")
{
    // balanceOf(vitalik.eth), byte-for-byte.
    CHECK(CallData("balanceOf(address)")
              .add_address("0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045")
              .to_hex()
        == "0x70a08231"
           "000000000000000000000000"
           "d8da6bf26964af9d7eed9e03e53415d37aa96045");

    // Bare hex, no 0x, works the same.
    CHECK(CallData("balanceOf(address)")
              .add_address("d8da6bf26964af9d7eed9e03e53415d37aa96045")
              .to_hex()
              .size()
        == 2 + 8 + 64);

    // Two-argument call: 4 + 32 + 32 bytes.
    const std::string transfer
        = CallData("transfer(address,uint256)")
              .add_address("0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045")
              .add_u256(U256::from_dec("1000000"))
              .to_hex();
    CHECK(transfer.size() == 2 + 8 + 64 + 64);
    CHECK(transfer.substr(0, 10) == "0xa9059cbb");
    CHECK(transfer.substr(transfer.size() - 8) == "000f4240"); // 1e6

    CHECK_THROWS_AS(CallData("balanceOf(address)").add_address("0x1234"),
        std::invalid_argument);
    CHECK_THROWS_AS(
        CallData("balanceOf(address)").add_address("zz" + std::string(38, '0')),
        std::invalid_argument);
}

TEST_CASE("decode_u256 takes exactly one word")
{
    CHECK(decode_u256("0x" + std::string(64, '0')).is_zero());

    std::string one = std::string(63, '0') + "1";
    CHECK(decode_u256("0x" + one) == U256::from_u64(1));
    CHECK(decode_u256(one) == U256::from_u64(1)); // prefix optional

    // "0x" is what eth_call returns for a non-contract address — that
    // is not a zero balance and must not decode into one.
    CHECK_THROWS_AS(decode_u256("0x"), std::invalid_argument);
    CHECK_THROWS_AS(
        decode_u256("0x" + std::string(63, '0')), std::invalid_argument);
    CHECK_THROWS_AS(
        decode_u256("0x" + std::string(65, '0')), std::invalid_argument);
}

TEST_CASE("to_bytes mirrors to_hex byte for byte")
{
    using izan::codec::CallData;
    const CallData call = [] {
        CallData c("transfer(address,uint256)");
        c.add_address("0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045")
            .add_u256(izan::units::U256::from_u64(1000000));
        return c;
    }();
    const std::vector<uint8_t> raw = call.to_bytes();
    REQUIRE(raw.size() == 4 + 32 + 32);
    // Same bytes the hex form spells — the transfer engine feeds these
    // straight into the transaction's data field.
    std::string spelled = "0x";
    static constexpr char digits[] = "0123456789abcdef";
    for (const uint8_t b : raw) {
        spelled += digits[b >> 4];
        spelled += digits[b & 0xf];
    }
    CHECK(spelled == call.to_hex());
    CHECK(spelled.substr(2, 8) == "a9059cbb");
    // 1'000'000 == 0x0f4240 rides in the last three bytes.
    CHECK(spelled.substr(spelled.size() - 6) == "0f4240");
}
