#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "domain/chains/chain_spec.hpp"
#include "domain/chains/jsonrpc.hpp"

using izan::chains::ChainRegistry;
using izan::chains::RpcError;

namespace {

const char* kTwoChains = R"([
    { "chain_id": 1, "name": "Ethereum", "symbol": "ETH",
      "rpc": ["https://ethereum-rpc.publicnode.com"],
      "explorer": "https://etherscan.io" },
    { "chain_id": 137, "name": "Polygon", "symbol": "POL", "decimals": 18,
      "rpc": ["https://polygon-rpc.com", "http://127.0.0.1:8545"] }
])";

}

TEST_CASE("chain registry parses config and applies defaults")
{
    ChainRegistry reg = ChainRegistry::from_json(kTwoChains);
    REQUIRE(reg.all().size() == 2);

    const auto* eth = reg.by_id(1);
    REQUIRE(eth);
    CHECK(eth->name == "Ethereum");
    CHECK(eth->symbol == "ETH");
    CHECK(eth->decimals == 18); // default when omitted
    CHECK(eth->explorer == "https://etherscan.io");
    REQUIRE(eth->rpc.size() == 1);

    const auto* pol = reg.by_id(137);
    REQUIRE(pol);
    CHECK(pol->explorer.empty());
    CHECK(pol->rpc.size() == 2);

    CHECK(reg.by_id(999) == nullptr);
}

TEST_CASE("chain registry rejects broken configs whole")
{
    CHECK_THROWS(ChainRegistry::from_json("not json"));
    CHECK_THROWS(ChainRegistry::from_json(R"({"chains": []})")); // not array
    // Duplicate id.
    CHECK_THROWS(ChainRegistry::from_json(R"([
        {"chain_id": 1, "name": "A", "symbol": "A", "rpc": ["https://x"]},
        {"chain_id": 1, "name": "B", "symbol": "B", "rpc": ["https://y"]}
    ])"));
    // Missing / zero id.
    CHECK_THROWS(ChainRegistry::from_json(
        R"([{"name": "A", "symbol": "A", "rpc": ["https://x"]}])"));
    // No rpc endpoints.
    CHECK_THROWS(ChainRegistry::from_json(
        R"([{"chain_id": 1, "name": "A", "symbol": "A", "rpc": []}])"));
    // Non-http scheme.
    CHECK_THROWS(ChainRegistry::from_json(
        R"([{"chain_id": 1, "name": "A", "symbol": "A", "rpc": ["wss://x"]}])"));
    // Missing symbol.
    CHECK_THROWS(ChainRegistry::from_json(
        R"([{"chain_id": 1, "name": "A", "rpc": ["https://x"]}])"));
}

TEST_CASE("shipped default chains.json is valid")
{
    std::ifstream f(IZAN_SOURCE_DIR "/data/chains.json");
    REQUIRE(f);
    std::stringstream ss;
    ss << f.rdbuf();
    ChainRegistry reg = ChainRegistry::from_json(ss.str());
    CHECK(reg.all().size() >= 4);
    CHECK(reg.by_id(1) != nullptr);
    CHECK(reg.by_id(8453) != nullptr);
    CHECK(reg.by_id(42161) != nullptr);
    CHECK(reg.by_id(137) != nullptr);
}

TEST_CASE("jsonrpc request envelope is exact")
{
    CHECK(
        izan::chains::make_request("eth_getBalance", R"(["0xabc","latest"])", 7)
        == R"({"jsonrpc":"2.0","id":7,"method":"eth_getBalance",)"
           R"("params":["0xabc","latest"]})");
    CHECK(izan::chains::make_request("eth_blockNumber", "[]", 1)
        == R"({"jsonrpc":"2.0","id":1,"method":"eth_blockNumber",)"
           R"("params":[]})");

    CHECK_THROWS_AS(
        izan::chains::make_request("m", "[oops", 1), std::invalid_argument);
}

TEST_CASE("jsonrpc response parsing: result, error, mismatch")
{
    const char* ok = R"({"jsonrpc":"2.0","id":7,"result":"0x1a"})";
    CHECK(izan::chains::result_string(ok, 7) == "0x1a");
    CHECK(izan::chains::result_of(ok, 7) == R"("0x1a")");

    // Structured results survive re-serialization.
    const char* obj = R"({"jsonrpc":"2.0","id":1,"result":{"number":"0x10"}})";
    CHECK(izan::chains::result_of(obj, 1).find("\"number\"")
        != std::string::npos);

    // Node-reported error surfaces code and message.
    const char* err
        = R"({"jsonrpc":"2.0","id":7,)"
          R"("error":{"code":-32601,"message":"method not found"}})";
    try {
        izan::chains::result_string(err, 7);
        FAIL("expected RpcError");
    } catch (const RpcError& e) {
        CHECK(e.code == -32601);
        CHECK(std::string(e.what()) == "method not found");
    }

    // Wrong id, garbage, and empty envelopes all refuse.
    CHECK_THROWS(izan::chains::result_string(ok, 8));
    CHECK_THROWS(izan::chains::result_string("garbage", 7));
    CHECK_THROWS(izan::chains::result_string(R"({"jsonrpc":"2.0","id":7})", 7));
    // A non-string result must not silently stringify.
    CHECK_THROWS(izan::chains::result_string(
        R"({"jsonrpc":"2.0","id":7,"result":26})", 7));
}
