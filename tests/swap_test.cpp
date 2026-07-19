// The LI.FI quote layer, judged against a captured real answer: a
// mainnet ETH→USDC quote saved verbatim (tests/data/lifi_quote.json),
// so the parser is graded on the aggregator's actual dialect, not on
// a hand-typed idea of it.

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/units/decimal.hpp"
#include "core/units/u256.hpp"
#include "domain/assets/balances.hpp"
#include "domain/swap/lifi.hpp"
#include "platform/net/http_client.hpp"

namespace {

std::string fixture()
{
    std::ifstream f(
        std::string(IZAN_SOURCE_DIR) + "/tests/data/lifi_quote.json",
        std::ios::binary);
    REQUIRE(f);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}

TEST_CASE("a captured quote parses into exactly what the wallet needs")
{
    const izan::swap::SwapQuote q = izan::swap::parse_quote(fixture());
    CHECK(q.tool == "kyberswap");
    CHECK(q.to == "0x1231DEB6f5749EF6cE6943a275A1D3E7486F4EaE");
    CHECK(q.approval_address == "0x1231DEB6f5749EF6cE6943a275A1D3E7486F4EaE");
    CHECK(q.from_amount.to_dec() == "100000000000000000");
    CHECK(q.to_amount.to_dec() == "186136643");
    CHECK(q.to_amount_min.to_dec() == "185205960");
    // value 0x16345785d8a0000 = the 0.1 ETH being sold.
    CHECK(q.value.to_dec() == "100000000000000000");
    CHECK(q.gas_limit == 0x12d652);
    REQUIRE(q.data.size() > 4);
    // The router entry point's selector leads the calldata.
    CHECK(q.data[0] == 0x73);
    CHECK(q.data[1] == 0x6e);
    CHECK(q.data[2] == 0xac);
    CHECK(q.data[3] == 0x0b);
}

TEST_CASE("half-read quotes are refused, never built")
{
    CHECK_THROWS(izan::swap::parse_quote("not json"));
    CHECK_THROWS(izan::swap::parse_quote("{}"));
    CHECK_THROWS(izan::swap::parse_quote(
        R"({"tool":"x","estimate":{"approvalAddress":"0x1",
            "fromAmount":"1","toAmount":"1","toAmountMin":"1"}})"));
}

TEST_CASE("the quote target spells every parameter")
{
    const std::string t = izan::swap::quote_target(1, izan::swap::kNativeToken,
        "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48",
        izan::units::U256::from_u64(5),
        "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "izan");
    CHECK(t.find("fromChain=1") != std::string::npos);
    CHECK(t.find("toChain=1") != std::string::npos);
    CHECK(t.find("fromAmount=5") != std::string::npos);
    CHECK(t.find("integrator=izan") != std::string::npos);
}

TEST_CASE("approve calldata wears the canonical selector")
{
    const auto data = izan::assets::erc20_approve_calldata(
        "0x1231DEB6f5749EF6cE6943a275A1D3E7486F4EaE",
        izan::units::U256::from_u64(1000000));
    REQUIRE(data.size() == 4 + 32 + 32);
    // approve(address,uint256) == 0x095ea7b3
    CHECK(data[0] == 0x09);
    CHECK(data[1] == 0x5e);
    CHECK(data[2] == 0xa7);
    CHECK(data[3] == 0xb3);
    // 1'000'000 == 0x0f4240 rides in the last three bytes.
    CHECK(data[data.size() - 3] == 0x0f);
    CHECK(data[data.size() - 2] == 0x42);
    CHECK(data[data.size() - 1] == 0x40);
}

//   IZAN_LIVE_TESTS=1 build/izan_tests.exe -tc="*live quote*"
TEST_CASE("live quote: the aggregator answers for mainnet ETH to USDC")
{
    if (!std::getenv("IZAN_LIVE_TESTS")) {
        MESSAGE("skipped (set IZAN_LIVE_TESTS=1 to run against li.quest)");
        return;
    }
    izan::net::HttpsClient client("li.quest", "443");
    const izan::swap::SwapQuote q = izan::swap::fetch_quote(client, 1,
        izan::swap::kNativeToken, "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48",
        izan::units::parse_units("0.1", 18),
        "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "izan");
    CHECK(!q.tool.empty());
    CHECK(!q.data.empty());
    CHECK(!q.to_amount_min.is_zero());
    MESSAGE("tool " << q.tool << " toAmountMin " << q.to_amount_min.to_dec());
}
