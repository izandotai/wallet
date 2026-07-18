#include <doctest/doctest.h>

#include "domain/assets/prices.hpp"

using namespace izan;

TEST_CASE("coingecko id table bridges the shipped symbols")
{
    CHECK(assets::coingecko_id("ETH") == "ethereum");
    CHECK(assets::coingecko_id("USDC") == "usd-coin");
    CHECK(assets::coingecko_id("USDC.e") == "usd-coin");
    CHECK(assets::coingecko_id("USDT") == "tether");
    CHECK(assets::coingecko_id("DAI") == "dai");
    CHECK(assets::coingecko_id("WETH") == "weth");
    // Unknown symbols price at nothing — never at a guess.
    CHECK(assets::coingecko_id("SCAMCOIN").empty());
    CHECK(assets::coingecko_id("").empty());
    CHECK(assets::coingecko_id("eth").empty()); // case is identity
}

TEST_CASE("price response parses id -> usd")
{
    const auto prices = assets::parse_usd_prices(
        R"({"ethereum":{"usd":3512.34},"usd-coin":{"usd":0.9998},)"
        R"("tether":{"usd":1.0001}})");
    REQUIRE(prices.size() == 3);
    CHECK(prices.at("ethereum") == doctest::Approx(3512.34));
    CHECK(prices.at("usd-coin") == doctest::Approx(0.9998));
    CHECK(prices.at("tether") == doctest::Approx(1.0001));
}

TEST_CASE("price response oddities")
{
    // Entries without a usd figure are skipped, not invented.
    const auto sparse = assets::parse_usd_prices(
        R"({"ethereum":{"eur":3300.0},"dai":{"usd":1.0}})");
    CHECK(sparse.size() == 1);
    CHECK(sparse.count("ethereum") == 0);

    CHECK(assets::parse_usd_prices("{}").empty());
    CHECK_THROWS(assets::parse_usd_prices("not json"));
    CHECK_THROWS(assets::parse_usd_prices("[1,2,3]"));
}
