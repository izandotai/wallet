// The Bitcoin read layer: address validation and the esplora address
// dialect, offline; one live round trip gated behind IZAN_LIVE_TESTS.

#include <doctest/doctest.h>

#include <cstdlib>

#include "domain/btc/coin_select.hpp"
#include "domain/btc/esplora.hpp"

TEST_CASE("bitcoin addresses validate by decoding across all four forms")
{
    using izan::btc::valid_address;
    // The genesis coinbase address (P2PKH).
    CHECK(valid_address("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa"));
    // P2SH, native segwit and taproot — the shipped preset quartet.
    CHECK(valid_address("3D9iyFHi1Zs9KoyynUfrL82rGhJfYTfSG4"));
    CHECK(valid_address("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"));
    CHECK(valid_address(
        "bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr"));
    CHECK_FALSE(valid_address(""));
    // One flipped character breaks the checksum, loudly.
    CHECK_FALSE(valid_address("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNb"));
    CHECK_FALSE(valid_address("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyv"));
    // Other families' addresses are refused, not guessed at.
    CHECK_FALSE(valid_address("0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045"));
    CHECK_FALSE(valid_address("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"));
}

TEST_CASE("the esplora address dialect parses and refuses the malformed")
{
    using izan::btc::parse_address_stats;
    CHECK(parse_address_stats(R"({"address":"x","chain_stats":
        {"funded_txo_sum":7500000000,"spent_txo_sum":500000000},
        "mempool_stats":{}})")
              .to_dec()
        == "7000000000");
    CHECK(parse_address_stats(
        R"({"chain_stats":{"funded_txo_sum":0,"spent_txo_sum":0}})")
            .is_zero());
    CHECK_THROWS(parse_address_stats("[]"));
    CHECK_THROWS(parse_address_stats(R"({"chain_stats":{}})"));
    // A ledger where more left than arrived is corrupt, not negative.
    CHECK_THROWS(parse_address_stats(R"({"chain_stats":
        {"funded_txo_sum":1,"spent_txo_sum":2}})"));
}

//   IZAN_LIVE_TESTS=1 build/izan_tests.exe -tc="*genesis*"
TEST_CASE("live: the genesis address still holds its tribute")
{
    if (!std::getenv("IZAN_LIVE_TESTS")) {
        MESSAGE("skipped (set IZAN_LIVE_TESTS=1 to run against mainnet)");
        return;
    }
    izan::chains::ChainSpec spec;
    spec.chain_id = 8332;
    spec.family = "btc";
    spec.rpc = { "https://mempool.space/api", "https://blockstream.info/api" };
    const auto sats
        = izan::btc::native_balance(spec, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa");
    // 50 BTC coinbase plus two decades of tributes.
    CHECK(sats.to_dec().size() >= 10);
    MESSAGE("genesis sats " << sats.to_dec());
}

TEST_CASE("an esplora tx page reads as our side of the ledger")
{
    // One incoming payment, one outgoing with change, one unconfirmed
    // straggler, one self-shuffle — only the first two are stories.
    const char* self = "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2";
    const std::string json = std::string(R"([
      {"txid":"aa11","status":{"confirmed":true,"block_time":1700000100},
       "vin":[{"prevout":{"scriptpubkey_address":"1SenderFace","value":5000}}],
       "vout":[{"scriptpubkey_address":")")
        + self + R"(","value":3000},
               {"scriptpubkey_address":"1SenderFace","value":1900}]},
      {"txid":"bb22","status":{"confirmed":true,"block_time":1700000200},
       "vin":[{"prevout":{"scriptpubkey_address":")"
        + self + R"(","value":10000}}],
       "vout":[{"scriptpubkey_address":"3DestFace","value":7000},
               {"scriptpubkey_address":")"
        + self + R"(","value":2500}]},
      {"txid":"cc33","status":{"confirmed":false},
       "vin":[],"vout":[{"scriptpubkey_address":")"
        + self + R"(","value":1}]},
      {"txid":"dd44","status":{"confirmed":true,"block_time":1700000300},
       "vin":[{"prevout":{"scriptpubkey_address":")"
        + self + R"(","value":800}}],
       "vout":[{"scriptpubkey_address":")"
        + self + R"(","value":800}]}
    ])";
    const auto txs = izan::btc::parse_txs(json, self);
    REQUIRE(txs.size() == 2);
    CHECK(txs[0].txid == "aa11");
    CHECK(txs[0].incoming);
    CHECK(txs[0].amount.to_dec() == "3000");
    CHECK(txs[0].counterparty == "1SenderFace");
    CHECK(txs[0].time == 1700000100);
    CHECK(txs[1].txid == "bb22");
    CHECK(!txs[1].incoming);
    // 10000 out, 2500 came home: the world got 7500, fee included.
    CHECK(txs[1].amount.to_dec() == "7500");
    CHECK(txs[1].counterparty == "3DestFace");

    CHECK_THROWS(izan::btc::parse_txs("{}", self));
}

TEST_CASE("the coin selector is deterministic and honest about dust")
{
    using izan::btc::Utxo;
    const std::vector<Utxo> purse = {
        { "aa", 0, 50000 },
        { "bb", 1, 30000 },
        { "cc", 0, 10000 },
        { "dd", 2, 546 },
    };
    // One big coin covers it: 1 input, 2 outputs, fee = 141 * 2.
    auto s = izan::btc::select_coins(purse, 20000, 2);
    REQUIRE(s.inputs.size() == 1);
    CHECK(s.inputs[0].txid == "aa");
    CHECK(s.fee == izan::btc::p2wpkh_vsize(1, 2) * 2);
    CHECK(s.change == 50000 - 20000 - s.fee);

    // Needs two coins; greedy takes the biggest first.
    auto s2 = izan::btc::select_coins(purse, 70000, 1);
    REQUIRE(s2.inputs.size() == 2);
    CHECK(s2.inputs[1].txid == "bb");
    CHECK(s2.change == 80000 - 70000 - s2.fee);

    // Change below dust folds into the fee and the outputs slim to one.
    const std::vector<Utxo> tight = { { "ee", 0, 21000 } };
    auto s3 = izan::btc::select_coins(tight, 20500, 1);
    CHECK(s3.change == 0);
    CHECK(s3.fee == 500); // everything left over, honestly labelled

    // Cannot afford: amount + fee over the whole purse.
    CHECK_THROWS(izan::btc::select_coins(purse, 90000, 2));
    CHECK_THROWS(izan::btc::select_coins(purse, 0, 2));

    // The sweep: everything in, one output, no change.
    auto sw = izan::btc::sweep_coins(purse, 2);
    CHECK(sw.inputs.size() == 4);
    CHECK(sw.fee == izan::btc::p2wpkh_vsize(4, 1) * 2);
    CHECK(sw.change == 0);
    CHECK_THROWS(
        izan::btc::sweep_coins({ { "ff", 0, 600 } }, 10)); // fee eats it

    // The esplora utxo page: unconfirmed entries stay out.
    const char* json = R"([
      {"txid":"a1","vout":0,"value":1000,"status":{"confirmed":true}},
      {"txid":"a2","vout":1,"value":2000,"status":{"confirmed":false}},
      {"txid":"a3","vout":0,"value":3000,"status":{"confirmed":true}}
    ])";
    const auto coins = izan::btc::parse_utxos(json);
    REQUIRE(coins.size() == 2);
    CHECK(coins[0].txid == "a1");
    CHECK(coins[1].value == 3000);
    CHECK_THROWS(izan::btc::parse_utxos("{}"));
}
