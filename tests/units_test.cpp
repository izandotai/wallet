#include <doctest/doctest.h>

#include <string>

#include "core/units/decimal.hpp"
#include "core/units/u256.hpp"

using izan::units::format_units;
using izan::units::parse_units;
using izan::units::U256;

namespace {

const std::string kMaxDec = "115792089237316195423570985008687907"
                            "853269984665640564039457584007913129639935";

}

TEST_CASE("u256 decimal roundtrip and bounds")
{
    CHECK(U256::from_u64(0).to_dec() == "0");
    CHECK(U256::from_u64(0).is_zero());
    CHECK(U256::from_u64(1234567890).to_dec() == "1234567890");
    CHECK(U256::from_dec("00123").to_dec() == "123");

    U256 max = U256::from_dec(kMaxDec);
    CHECK(max.to_dec() == kMaxDec);
    for (uint8_t b : max.be)
        CHECK(b == 0xff);

    CHECK_THROWS_AS(U256::from_dec(kMaxDec + "0"), std::overflow_error);
    CHECK_THROWS_AS(U256::from_dec(""), std::invalid_argument);
    CHECK_THROWS_AS(U256::from_dec("12a3"), std::invalid_argument);
}

TEST_CASE("u256 hex quantity parsing and emission")
{
    // One ether in wei, the best-known quantity constant there is.
    CHECK(
        U256::from_dec("1000000000000000000").to_hex() == "0xde0b6b3a7640000");
    CHECK(
        U256::from_hex("0xde0b6b3a7640000").to_dec() == "1000000000000000000");

    CHECK(U256::from_hex("0x1a").to_dec() == "26");
    CHECK(U256::from_hex("0X1A").to_dec() == "26");
    CHECK(U256::from_hex("ff").to_dec() == "255");
    CHECK(U256::from_hex("0x0").is_zero());

    CHECK(U256::from_u64(0).to_hex() == "0x0");
    CHECK(U256::from_u64(15).to_hex() == "0xf");
    CHECK(U256::from_u64(16).to_hex() == "0x10");
    CHECK(U256::from_u64(255).to_hex() == "0xff");

    CHECK_THROWS_AS(U256::from_hex(""), std::invalid_argument);
    CHECK_THROWS_AS(U256::from_hex("0x"), std::invalid_argument);
    CHECK_THROWS_AS(U256::from_hex("0xg1"), std::invalid_argument);
    CHECK_THROWS_AS(
        U256::from_hex(std::string(65, 'a')), std::invalid_argument);

    // 64 nibbles is exactly full width.
    CHECK(U256::from_hex(std::string(64, 'f')).to_dec() == kMaxDec);
}

TEST_CASE("u256 ordering follows numeric value")
{
    CHECK(U256::from_u64(2) < U256::from_u64(10));
    CHECK(U256::from_dec("999999999999999999999")
        < U256::from_dec("1000000000000000000000"));
    CHECK(U256::from_u64(7) == U256::from_dec("7"));
    CHECK(U256::from_dec(kMaxDec) > U256::from_u64(UINT64_MAX));
}

TEST_CASE("u256 checked arithmetic refuses to wrap")
{
    CHECK(
        U256::from_u64(1).checked_add(U256::from_u64(2)) == U256::from_u64(3));
    // Carry across every limb boundary.
    CHECK(U256::from_u64(UINT64_MAX).checked_add(U256::from_u64(1)).to_hex()
        == "0x10000000000000000");
    CHECK(U256::from_u64(3).checked_sub(U256::from_u64(3)).is_zero());

    U256 max = U256::from_dec(kMaxDec);
    CHECK(max.checked_sub(max).is_zero());
    CHECK_THROWS_AS(max.checked_add(U256::from_u64(1)), std::overflow_error);
    CHECK_THROWS_AS(
        U256::from_u64(0).checked_sub(U256::from_u64(1)), std::underflow_error);
}

TEST_CASE("format_units renders exact human decimals")
{
    auto wei = [](const char* d) { return U256::from_dec(d); };

    CHECK(format_units(wei("1000000000000000000"), 18) == "1");
    CHECK(format_units(wei("1500000000000000000"), 18) == "1.5");
    CHECK(format_units(wei("1"), 18) == "0.000000000000000001");
    CHECK(format_units(wei("0"), 18) == "0");
    CHECK(format_units(wei("123"), 0) == "123");
    // USDC-style 6 decimals.
    CHECK(format_units(wei("1234567"), 6) == "1.234567");
    CHECK(format_units(wei("1230000"), 6) == "1.23");
    CHECK(format_units(U256::from_dec(kMaxDec), 18)
        == "115792089237316195423570985008687907853269984665640564039457"
           ".584007913129639935");

    CHECK_THROWS_AS(format_units(wei("1"), 78), std::invalid_argument);
}

TEST_CASE("parse_units accepts exact amounts and nothing else")
{
    CHECK(parse_units("1.5", 18).to_dec() == "1500000000000000000");
    CHECK(parse_units("0.000000000000000001", 18).to_dec() == "1");
    CHECK(parse_units("123", 0).to_dec() == "123");
    CHECK(parse_units("1.500000", 6).to_dec() == "1500000");
    CHECK(parse_units("007", 18).to_dec() == "7000000000000000000");

    // format → parse roundtrip at full width.
    U256 max = U256::from_dec(kMaxDec);
    CHECK(parse_units(format_units(max, 18), 18) == max);

    CHECK_THROWS_AS(parse_units("", 18), std::invalid_argument);
    CHECK_THROWS_AS(parse_units("1.", 18), std::invalid_argument);
    CHECK_THROWS_AS(parse_units(".5", 18), std::invalid_argument);
    CHECK_THROWS_AS(parse_units("1.2.3", 18), std::invalid_argument);
    CHECK_THROWS_AS(parse_units("-1", 18), std::invalid_argument);
    CHECK_THROWS_AS(parse_units("1,5", 18), std::invalid_argument);
    CHECK_THROWS_AS(parse_units(" 1", 18), std::invalid_argument);
    // More precision than the token carries must never round silently.
    CHECK_THROWS_AS(parse_units("0.1234567", 6), std::invalid_argument);
    CHECK_THROWS_AS(parse_units("0.1", 0), std::invalid_argument);
}
