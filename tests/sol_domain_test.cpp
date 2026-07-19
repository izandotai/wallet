// The Solana read layer: address validation and the getBalance
// dialect, judged offline; one live round trip gated behind
// IZAN_LIVE_TESTS.

#include <doctest/doctest.h>

#include <cstdlib>

#include "domain/chains/chain_spec.hpp"
#include "domain/sol/solana.hpp"

TEST_CASE("solana addresses validate by decoding, never by eye")
{
    using izan::sol::valid_address;
    // The USDC mint — as canonical as a Solana address gets.
    CHECK(valid_address("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"));
    // The system program: mostly '1's, still exactly 32 bytes.
    CHECK(valid_address("11111111111111111111111111111111"));
    CHECK_FALSE(valid_address(""));
    CHECK_FALSE(valid_address("notanaddress"));
    // 0x-hex speaks EVM, not base58.
    CHECK_FALSE(valid_address("0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045"));
    // 'l' is not in the base58 alphabet.
    CHECK_FALSE(valid_address("l1111111111111111111111111111111"));
    // A 64-byte keypair string is a SECRET, not an address.
    CHECK_FALSE(valid_address(std::string(88, '2')));
}

TEST_CASE("the getBalance dialect parses and refuses malformed answers")
{
    using izan::sol::parse_balance_result;
    CHECK(parse_balance_result(R"({"context":{"slot":362},"value":88899235})")
              .to_dec()
        == "88899235");
    CHECK(parse_balance_result(R"({"value":0})").is_zero());
    CHECK_THROWS(parse_balance_result("[]"));
    CHECK_THROWS(parse_balance_result(R"({"context":{}})"));
    CHECK_THROWS(parse_balance_result("not json"));
}

//   IZAN_LIVE_TESTS=1 build/izan_tests.exe -tc="*solana answers*"
TEST_CASE("live: solana answers a balance for the USDC mint")
{
    if (!std::getenv("IZAN_LIVE_TESTS")) {
        MESSAGE("skipped (set IZAN_LIVE_TESTS=1 to run against mainnet)");
        return;
    }
    izan::chains::ChainSpec spec;
    spec.chain_id = 501;
    spec.name = "Solana";
    spec.symbol = "SOL";
    spec.decimals = 9;
    spec.family = "sol";
    spec.rpc = { "https://api.mainnet-beta.solana.com",
        "https://solana-rpc.publicnode.com" };
    izan::chains::RpcClient rpc(spec);
    // The mint account is rent-exempt: its lamports are never zero.
    const auto lamports = izan::sol::native_balance(
        rpc, "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
    CHECK_FALSE(lamports.is_zero());
    MESSAGE("USDC mint lamports " << lamports.to_dec());
}
