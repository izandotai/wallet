#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "core/codec/abi.hpp"
#include "core/units/decimal.hpp"
#include "domain/assets/portfolio.hpp"

using izan::assets::PortfolioReader;
using izan::assets::TokenRegistry;
using izan::chains::ChainRegistry;
using izan::units::U256;

namespace {

const char* kVitalik = "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045";

std::string slurp(const char* path)
{
    std::ifstream f(path);
    REQUIRE(f);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

ChainRegistry offline_chain()
{
    return ChainRegistry::from_json(R"([
        { "chain_id": 1, "name": "Test", "symbol": "ETH",
          "rpc": ["https://127.0.0.1:1"] }
    ])");
}

}

TEST_CASE("token registry validates and normalizes")
{
    // Lowercase input comes back checksummed.
    TokenRegistry reg = TokenRegistry::from_json(R"([
        { "chain_id": 1,
          "address": "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48",
          "symbol": "USDC", "decimals": 6 }
    ])");
    REQUIRE(reg.all().size() == 1);
    CHECK(reg.all()[0].address == "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48");
    CHECK(reg.tokens_for(1).size() == 1);
    CHECK(reg.tokens_for(2).empty());

    CHECK_THROWS(TokenRegistry::from_json("null"));
    // Bad address.
    CHECK_THROWS(TokenRegistry::from_json(R"([
        { "chain_id": 1, "address": "0x1234", "symbol": "X", "decimals": 6 }
    ])"));
    // Missing symbol.
    CHECK_THROWS(TokenRegistry::from_json(R"([
        { "chain_id": 1,
          "address": "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48",
          "decimals": 6 }
    ])"));
    // Duplicate (chain, address) pair, case-insensitive.
    CHECK_THROWS(TokenRegistry::from_json(R"([
        { "chain_id": 1,
          "address": "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48",
          "symbol": "A", "decimals": 6 },
        { "chain_id": 1,
          "address": "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48",
          "symbol": "B", "decimals": 6 }
    ])"));
    // Decimals beyond U256 range.
    CHECK_THROWS(TokenRegistry::from_json(R"([
        { "chain_id": 1,
          "address": "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48",
          "symbol": "X", "decimals": 78 }
    ])"));
}

TEST_CASE("portfolio rejects tokens on chains the registry lacks")
{
    TokenRegistry tokens = TokenRegistry::from_json(R"([
        { "chain_id": 999,
          "address": "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48",
          "symbol": "GHOST", "decimals": 6 }
    ])");
    CHECK_THROWS(PortfolioReader(offline_chain(), std::move(tokens)));
}

TEST_CASE("snapshot keeps failed reads as rows, not silence")
{
    TokenRegistry tokens = TokenRegistry::from_json(R"([
        { "chain_id": 1,
          "address": "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48",
          "symbol": "USDC", "decimals": 6 }
    ])");
    PortfolioReader reader(offline_chain(), std::move(tokens));

    CHECK_THROWS_AS(reader.snapshot("0xdead"), std::invalid_argument);

    auto rows = reader.snapshot(kVitalik);
    REQUIRE(rows.size() == 2); // native + one token
    CHECK(rows[0].symbol == "ETH");
    CHECK(rows[0].token.empty());
    CHECK(rows[1].symbol == "USDC");
    CHECK(!rows[1].token.empty());
    for (const auto& r : rows) {
        CHECK(!r.ok);
        CHECK(!r.error.empty());
        CHECK(r.amount.is_zero());
    }
}

// Full watch-only proof against every shipped chain, plus a config
// audit: each shipped token must answer decimals() with the configured
// value — a wrong address or wrong decimals cannot survive this.
TEST_CASE("live: shipped config snapshot and on-chain decimals audit")
{
    if (!std::getenv("IZAN_LIVE_TESTS")) {
        MESSAGE("skipped (set IZAN_LIVE_TESTS=1 to run against mainnet)");
        return;
    }

    ChainRegistry chains
        = ChainRegistry::from_json(slurp(IZAN_SOURCE_DIR "/data/chains.json"));
    TokenRegistry tokens
        = TokenRegistry::from_json(slurp(IZAN_SOURCE_DIR "/data/tokens.json"));

    // decimals() audit first, chain by chain.
    const std::string decimalsData
        = izan::codec::CallData("decimals()").to_hex();
    for (const auto& t : tokens.all()) {
        CAPTURE(t.symbol);
        CAPTURE(t.address);
        izan::chains::RpcClient rpc(*chains.by_id(t.chain_id));
        U256 onChain = izan::codec::decode_u256(rpc.call_string("eth_call",
            "[{\"to\":\"" + t.address + "\",\"data\":\"" + decimalsData
                + "\"},\"latest\"]"));
        CHECK(onChain == U256::from_u64(t.decimals));
    }

    PortfolioReader reader(std::move(chains), std::move(tokens));
    auto rows = reader.snapshot(kVitalik);
    REQUIRE(rows.size() >= 14); // 5 chains native + 9 tokens
    bool sawMainnetEth = false;
    for (const auto& r : rows) {
        CAPTURE(r.chain);
        CAPTURE(r.symbol);
        CHECK(r.ok);
        if (r.chain_id == 1 && r.token.empty()) {
            sawMainnetEth = true;
            CHECK(r.amount > izan::units::parse_units("0.001", 18));
        }
    }
    CHECK(sawMainnetEth);
}
