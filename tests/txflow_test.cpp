#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/units/u256.hpp"
#include "domain/chains/chain_spec.hpp"
#include "domain/chains/jsonrpc.hpp"
#include "domain/tx/txflow.hpp"

#include "data/eip1559_vector.inc"

using izan::chains::ChainRegistry;
using izan::chains::ChainSpec;
using izan::chains::RpcClient;
using izan::tx::Eip1559Tx;
using izan::units::U256;

namespace {

const char* kVitalik = "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045";

std::vector<uint8_t> unhex(std::string_view hex)
{
    auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9')
            return uint8_t(c - '0');
        if (c >= 'a' && c <= 'f')
            return uint8_t(c - 'a' + 10);
        REQUIRE(false);
        return 0;
    };
    std::vector<uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(uint8_t(nib(hex[i]) << 4 | nib(hex[i + 1])));
    return out;
}

RpcClient unreachable_rpc()
{
    ChainSpec spec { .chain_id = 1,
        .name = "T",
        .symbol = "T",
        .rpc = { "https://127.0.0.1:1" } };
    return RpcClient(std::move(spec));
}

ChainRegistry shipped_registry()
{
    std::ifstream f(IZAN_SOURCE_DIR "/data/chains.json");
    REQUIRE(f);
    std::stringstream ss;
    ss << f.rdbuf();
    return ChainRegistry::from_json(ss.str());
}

}

TEST_CASE("txflow: bad input never reaches the network")
{
    RpcClient rpc = unreachable_rpc();

    CHECK_THROWS_AS(izan::tx::next_nonce(rpc, "0x1234"), std::invalid_argument);
    CHECK_THROWS_AS(
        izan::tx::next_nonce(rpc, "vitalik.eth"), std::invalid_argument);

    Eip1559Tx draft;
    CHECK_THROWS_AS(izan::tx::estimate_gas(rpc, "not-an-address", draft),
        std::invalid_argument);

    CHECK_THROWS_AS(izan::tx::broadcast(rpc, std::vector<uint8_t> {}),
        std::invalid_argument);
}

TEST_CASE("txflow: receipt parsing accepts the two honest shapes only")
{
    // Pending / unknown transaction: result is JSON null.
    CHECK(!izan::tx::parse_receipt("null").has_value());

    // A mined, successful transaction.
    const char* ok = R"({"status":"0x1","blockNumber":"0x185f194",)"
                     R"("gasUsed":"0x5208","effectiveGasPrice":"0x77359400",)"
                     R"("transactionHash":"0xabc","logs":[]})";
    auto r = izan::tx::parse_receipt(ok);
    REQUIRE(r.has_value());
    CHECK(r->success);
    CHECK(r->block_number == 0x185f194);
    CHECK(r->gas_used == 21000);
    CHECK(r->effective_gas_price == U256::from_dec("2000000000"));

    // A mined, reverted transaction is a real answer, not an error.
    const char* reverted
        = R"({"status":"0x0","blockNumber":"0x1","gasUsed":"0x5208",)"
          R"("effectiveGasPrice":"0x1"})";
    auto rv = izan::tx::parse_receipt(reverted);
    REQUIRE(rv.has_value());
    CHECK(!rv->success);
}

TEST_CASE("txflow: malformed receipts refuse instead of guessing")
{
    CHECK_THROWS(izan::tx::parse_receipt("garbage"));
    CHECK_THROWS(izan::tx::parse_receipt(R"("0x1")")); // not an object
    // Missing status.
    CHECK_THROWS(izan::tx::parse_receipt(
        R"({"blockNumber":"0x1","gasUsed":"0x1","effectiveGasPrice":"0x1"})"));
    // Status outside 0x0/0x1 (pre-Byzantium root, or nonsense).
    CHECK_THROWS(izan::tx::parse_receipt(
        R"({"status":"0x2","blockNumber":"0x1","gasUsed":"0x1",)"
        R"("effectiveGasPrice":"0x1"})"));
    // A block number beyond 64 bits means a broken node, not block
    // eighteen quintillion.
    CHECK_THROWS(izan::tx::parse_receipt(
        R"({"status":"0x1","blockNumber":"0x10000000000000000",)"
        R"("gasUsed":"0x1","effectiveGasPrice":"0x1"})"));
}

TEST_CASE("txflow: base fee comes from the block or not at all")
{
    CHECK(izan::tx::parse_base_fee(R"({"baseFeePerGas":"0x77359400"})")
        == U256::from_dec("2000000000"));

    CHECK_THROWS(izan::tx::parse_base_fee("garbage"));
    CHECK_THROWS(izan::tx::parse_base_fee("null")); // no such block
    // Pre-London block: pricing a type-2 tx there is an error, not a
    // zero-fee bargain.
    CHECK_THROWS(izan::tx::parse_base_fee(R"({"number":"0x1"})"));
}

// End-to-end quoting and receipt reads against mainnet. Opt-in:
//   IZAN_LIVE_TESTS=1 build/izan_tests.exe
TEST_CASE("live: nonce, gas quote and golden receipt through the full stack")
{
    if (!std::getenv("IZAN_LIVE_TESTS")) {
        MESSAGE("skipped (set IZAN_LIVE_TESTS=1 to run against mainnet)");
        return;
    }

    ChainRegistry reg = shipped_registry();
    const ChainSpec* eth = reg.by_id(1);
    REQUIRE(eth);
    RpcClient rpc(*eth);

    // vitalik.eth has been transacting since 2015; pending nonce is
    // deep into four digits and can only grow.
    CHECK(izan::tx::next_nonce(rpc, kVitalik) > 1000);

    // A plain value transfer costs exactly the intrinsic 21000 gas.
    Eip1559Tx draft;
    draft.value = U256::from_u64(1);
    CHECK(izan::tx::estimate_gas(rpc, kVitalik, draft) == 21000);

    // The golden-vector transaction is mined history; its receipt must
    // agree with the block recorded when the vector was cut, and the
    // effective price must sit inside the fee cap it signed.
    const std::vector<uint8_t> hash = unhex(kVecHash);
    REQUIRE(hash.size() == 32);
    std::array<uint8_t, 32> h {};
    std::memcpy(h.data(), hash.data(), 32);
    auto receipt = izan::tx::transaction_receipt(rpc, h);
    REQUIRE(receipt.has_value());
    CHECK(receipt->success);
    CHECK(receipt->block_number == kVecBlock);
    CHECK(receipt->gas_used <= kVecGas);
    CHECK(!receipt->effective_gas_price.is_zero());
    CHECK(receipt->effective_gas_price <= U256::from_hex(kVecMaxFee));

    // A live fee quote must be internally consistent: the cap covers
    // a doubled base fee plus the tip, and mainnet's base fee is never
    // zero.
    const auto fees = izan::tx::quote_fees(rpc);
    CHECK(!fees.base_fee_per_gas.is_zero());
    CHECK(fees.max_fee_per_gas
        == fees.base_fee_per_gas.checked_add(fees.base_fee_per_gas)
            .checked_add(fees.max_priority_fee_per_gas));

    // A hash nobody ever mined: the pending/unknown path.
    std::array<uint8_t, 32> never {};
    never.fill(0x11);
    CHECK(!izan::tx::transaction_receipt(rpc, never).has_value());

    // Broadcast path: garbage raw bytes must come back as the node's
    // own verdict (RpcError), proving the request reached a healthy
    // node — without spending anything.
    const std::vector<uint8_t> junk = { 0x02, 0xc0 };
    CHECK_THROWS_AS(izan::tx::broadcast(rpc, junk), izan::chains::RpcError);
}
