// The Solana read layer: address validation and the getBalance
// dialect, judged offline; one live round trip gated behind
// IZAN_LIVE_TESTS.

#include <doctest/doctest.h>

#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <span>
#include <thread>

#include <sodium.h>

extern "C" {
#include <base58.h>
}

#include "core/crypto/sol.hpp"
#include "domain/chains/chain_spec.hpp"
#include "domain/sol/sol_tx.hpp"
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

TEST_CASE("a signature feed keeps its moments and its failures")
{
    const char* json = R"([
      {"signature":"5sigAlive","slot":1,"blockTime":1700000500,"err":null},
      {"signature":"4sigDead","slot":2,"blockTime":null,
       "err":{"InstructionError":[0,"Custom"]}}
    ])";
    const auto sigs = izan::sol::parse_signatures(json);
    REQUIRE(sigs.size() == 2);
    CHECK(sigs[0].signature == "5sigAlive");
    CHECK(sigs[0].time == 1700000500);
    CHECK(!sigs[0].failed);
    CHECK(sigs[1].signature == "4sigDead");
    CHECK(sigs[1].time == 0); // the node lost the moment, not the row
    CHECK(sigs[1].failed);

    CHECK_THROWS(izan::sol::parse_signatures("{\"value\":1}"));
}

TEST_CASE("compact-u16 walks its boundaries both ways")
{
    auto enc = [](uint16_t v) {
        std::vector<uint8_t> out;
        izan::sol::put_compact_u16(out, v);
        return out;
    };
    CHECK(enc(0) == std::vector<uint8_t> { 0x00 });
    CHECK(enc(127) == std::vector<uint8_t> { 0x7f });
    CHECK(enc(128) == std::vector<uint8_t> { 0x80, 0x01 });
    CHECK(enc(16383) == std::vector<uint8_t> { 0xff, 0x7f });
    CHECK(enc(16384) == std::vector<uint8_t> { 0x80, 0x80, 0x01 });
    CHECK(enc(65535) == std::vector<uint8_t> { 0xff, 0xff, 0x03 });
    for (uint32_t v : { 0u, 127u, 128u, 16383u, 16384u, 65535u }) {
        const auto bytes = enc(uint16_t(v));
        std::size_t pos = 0;
        CHECK(izan::sol::take_compact_u16(bytes, pos) == v);
        CHECK(pos == bytes.size());
    }
    std::size_t pos = 0;
    CHECK_THROWS(izan::sol::take_compact_u16(
        std::vector<uint8_t> { 0x80, 0x80, 0x80, 0x01 }, pos));
}

TEST_CASE("a transfer message survives the round trip and nothing else passes")
{
    const char* alice = "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk";
    const char* bob = "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v";
    std::array<uint8_t, 32> hash {};
    for (int i = 0; i < 32; ++i)
        hash[std::size_t(i)] = uint8_t(i + 1);
    const auto msg
        = izan::sol::encode_transfer_message(alice, bob, 12345678, hash);
    // Fixed grammar: 3 header + 1 count + 96 keys + 32 hash + 1 count
    // + 1 idx + 1 count + 2 accounts + 1 len + 12 data.
    CHECK(msg.size() == 150);
    const auto back = izan::sol::parse_transfer_message(msg);
    CHECK(back.from == alice);
    CHECK(back.to == bob);
    CHECK(back.lamports == 12345678);
    CHECK(back.blockhash == hash);

    CHECK_THROWS(izan::sol::encode_transfer_message(alice, bob, 0, hash));
    CHECK_THROWS(izan::sol::encode_transfer_message("junk", bob, 1, hash));

    // The whitelist: every tampered shape must be refused.
    auto tamper = [&](std::size_t at, uint8_t v) {
        auto bad = msg;
        bad[at] = v;
        return bad;
    };
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(0, 2)));   // 2 sigs
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(3, 4)));   // 4 accts
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(70, 1)));  // program
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(132, 2))); // 2 instr
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(133, 1))); // prog idx
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(138, 1))); // tag
    auto truncated = msg;
    truncated.pop_back();
    CHECK_THROWS(izan::sol::parse_transfer_message(truncated));
    auto trailing = msg;
    trailing.push_back(0);
    CHECK_THROWS(izan::sol::parse_transfer_message(trailing));

    // signature || message, one signature slot.
    std::array<uint8_t, 64> sig {};
    sig[0] = 0xAA;
    const auto tx = izan::sol::assemble_tx(sig, msg);
    CHECK(tx.size() == 1 + 64 + msg.size());
    CHECK(tx[0] == 1);
    CHECK(tx[1] == 0xAA);
}

TEST_CASE("ed25519 signing matches RFC 8032 and its own public key")
{
    auto unhex = [](const char* h) {
        std::vector<uint8_t> out;
        for (const char* p = h; p[0] && p[1]; p += 2) {
            auto nib = [](char c) {
                return uint8_t(c <= '9' ? c - '0' : c - 'a' + 10);
            };
            out.push_back(uint8_t(nib(p[0]) << 4 | nib(p[1])));
        }
        return out;
    };
    // RFC 8032 §7.1 TEST 1: the empty message.
    std::array<uint8_t, 32> seed1 {};
    const auto s1 = unhex("9d61b19deffd5a60ba844af492ec2cc4"
                          "4449c5697b326919703bac031cae7f60");
    std::copy(s1.begin(), s1.end(), seed1.begin());
    const auto sig1 = izan::crypto::sol_sign(seed1, {});
    CHECK(std::vector<uint8_t>(sig1.begin(), sig1.end())
        == unhex("e5564300c360ac729086e2cc806e828a"
                 "84877f1eb8e5d974d873e06522490155"
                 "5fb8821590a33bacc61e39701cf9b46b"
                 "d25bf5f0595bbe24655141438e7a100b"));
    // TEST 2: the one-byte message 0x72.
    std::array<uint8_t, 32> seed2 {};
    const auto s2 = unhex("4ccd089b28ff96da9db6c346ec114e0f"
                          "5b8a319f35aba624da8cf6ed4fb8a6fb");
    std::copy(s2.begin(), s2.end(), seed2.begin());
    const uint8_t msg2[] = { 0x72 };
    const auto sig2 = izan::crypto::sol_sign(seed2, msg2);
    CHECK(std::vector<uint8_t>(sig2.begin(), sig2.end())
        == unhex("92a009a9f0d4cab8720e820b5f642540"
                 "a2b27b5416503f8fb3762223ebdb69da"
                 "085ac1e43e15996e458f3613d0f11d8c"
                 "387b2eaeb4302aeeb00d291612bb0c00"));
}

TEST_CASE("the send flow's answers parse offline")
{
    // getLatestBlockhash: the base58 hash back to its 32 bytes.
    const auto hash = izan::sol::parse_blockhash_result(
        R"({"context":{"slot":1},"value":{"blockhash":)"
        R"("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk",)"
        R"("lastValidBlockHeight":100}})");
    CHECK(hash[0] != 0); // decoded, not defaulted
    CHECK_THROWS(izan::sol::parse_blockhash_result(
        R"({"value":{"blockhash":"tooshort"}})"));

    // getSignatureStatuses: the four fates plus the unknown.
    using izan::sol::SigStatus;
    auto status = [](const char* entry) {
        return izan::sol::parse_signature_status(
            std::string(R"({"context":{"slot":1},"value":[)") + entry + "]}");
    };
    CHECK(status("null") == SigStatus::Unknown);
    CHECK(status(R"({"slot":5,"err":null,"confirmationStatus":"processed"})")
        == SigStatus::Processed);
    CHECK(status(R"({"slot":5,"err":null,"confirmationStatus":"confirmed"})")
        == SigStatus::Confirmed);
    CHECK(status(R"({"slot":5,"err":null,"confirmationStatus":"finalized"})")
        == SigStatus::Finalized);
    CHECK(status(R"({"slot":5,"err":{"InstructionError":[0,1]},)"
                 R"("confirmationStatus":"finalized"})")
        == SigStatus::Failed);
    CHECK_THROWS(izan::sol::parse_signature_status(R"({"value":[]})"));
}

// The chain itself judges the whole send stack: airdropped devnet
// lamports, a transfer encoded, signed and broadcast by our own code,
// confirmed by consensus. Run with:
//   IZAN_SOL_DEVNET=1 build/izan_tests.exe -tc="*devnet*"
TEST_CASE("a devnet transfer survives the real chain"
    * doctest::skip(std::getenv("IZAN_SOL_DEVNET") == nullptr))
{
    izan::chains::ChainSpec spec;
    spec.chain_id = 502;
    spec.name = "Solana Devnet";
    spec.symbol = "SOL";
    spec.decimals = 9;
    spec.rpc = { "https://api.devnet.solana.com" };
    spec.testnet = true;
    spec.family = "sol";
    izan::chains::RpcClient rpc(spec);

    // Two fresh identities: a funded sender, an empty recipient.
    std::array<uint8_t, 32> seed {};
    randombytes_buf(seed.data(), seed.size());
    const std::string sender = izan::crypto::sol_key_address(seed);
    std::array<uint8_t, 32> rseed {};
    randombytes_buf(rseed.data(), rseed.size());
    const std::string receiver = izan::crypto::sol_key_address(rseed);

    // Devnet faucet: rate limits are weather, not a code verdict.
    std::string drop;
    try {
        drop = rpc.call("requestAirdrop", "[\"" + sender + "\",1000000000]");
    } catch (const std::exception& e) {
        MESSAGE("devnet faucet unavailable: ", std::string(e.what()));
        return;
    }
    // The answer is the airdrop's signature, quoted.
    REQUIRE(drop.size() > 2);
    const std::string drop_sig = drop.substr(1, drop.size() - 2);
    bool funded = false;
    for (int i = 0; i < 30 && !funded; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const auto st = izan::sol::signature_status(rpc, drop_sig);
        funded = st == izan::sol::SigStatus::Confirmed
            || st == izan::sol::SigStatus::Finalized;
    }
    if (!funded) {
        MESSAGE("airdrop never confirmed; devnet weather, aborting live leg");
        return;
    }

    // Enough to clear the recipient's rent floor, twice over.
    const uint64_t rent = izan::sol::rent_exempt_minimum(rpc);
    const uint64_t lamports = rent * 2;
    const auto hash = izan::sol::latest_blockhash(rpc);
    const auto msg
        = izan::sol::encode_transfer_message(sender, receiver, lamports, hash);
    const auto sig = izan::crypto::sol_sign(seed, msg);
    const std::string tx_sig
        = izan::sol::send_transaction(rpc, izan::sol::assemble_tx(sig, msg));
    REQUIRE(!tx_sig.empty());

    bool landed = false;
    for (int i = 0; i < 30 && !landed; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const auto st = izan::sol::signature_status(rpc, tx_sig);
        REQUIRE(st != izan::sol::SigStatus::Failed);
        landed = st == izan::sol::SigStatus::Confirmed
            || st == izan::sol::SigStatus::Finalized;
    }
    REQUIRE(landed);
    CHECK(izan::sol::native_balance(rpc, receiver).to_dec()
        == std::to_string(lamports));
    MESSAGE("devnet transfer confirmed: ", tx_sig);
}

TEST_CASE("a self-transfer wears the two-key shape and reads back whole")
{
    const char* me = "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk";
    std::array<uint8_t, 32> hash {};
    hash.fill(0x22);
    const auto msg = izan::sol::encode_transfer_message(me, me, 7777, hash);
    // 3 header + 1 count + 64 keys + 32 hash + 1 + 1 + 1 + 2 + 1 + 12.
    CHECK(msg.size() == 118);
    const auto back = izan::sol::parse_transfer_message(msg);
    CHECK(back.from == me);
    CHECK(back.to == me);
    CHECK(back.lamports == 7777);

    // The whitelist holds for this shape too: wrong program index,
    // wrong account refs, foreign program bytes — all refused.
    auto tamper = [&](std::size_t at, uint8_t v) {
        auto bad = msg;
        bad[at] = v;
        return bad;
    };
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(3, 4)));
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(38, 1)));  // program
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(101, 2))); // idx
    CHECK_THROWS(izan::sol::parse_transfer_message(tamper(104, 1))); // refs
}

TEST_CASE("SPL holdings parse and the majors answer to their names")
{
    const char* json = R"({"context":{"slot":1},"value":[
      {"pubkey":"AtaOne11111111111111111111111111111111111111",
       "account":{"data":{"parsed":{"info":{
         "mint":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v",
         "tokenAmount":{"amount":"2500000","decimals":6,
                        "uiAmountString":"2.5"}}}}}},
      {"pubkey":"AtaTwo11111111111111111111111111111111111111",
       "account":{"data":{"parsed":{"info":{
         "mint":"StrangeMint111111111111111111111111111111111",
         "tokenAmount":{"amount":"0","decimals":9,
                        "uiAmountString":"0"}}}}}},
      {"pubkey":"Broken","account":{"data":{"parsed":{"info":{
         "mint":"NoAmount1111111111111111111111111111111111111"}}}}}
    ]})";
    const auto holdings = izan::sol::parse_token_accounts(json);
    REQUIRE(holdings.size() == 2);
    CHECK(holdings[0].mint == "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
    CHECK(holdings[0].amount == 2500000);
    CHECK(holdings[0].decimals == 6);
    CHECK(holdings[1].amount == 0); // empty ATAs survive; callers filter
    CHECK_THROWS(izan::sol::parse_token_accounts("[]"));

    CHECK(izan::sol::known_mint_symbol(
              "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v")
        == "USDC");
    CHECK(izan::sol::known_mint_symbol("StrangeMint").empty());
}

TEST_CASE("the ATA falls off the curve exactly where the duel says")
{
    // Anchor generated by the independent pure-python implementation
    // in tests/data/derive_vectors.py (PDA + ed25519 decompression):
    // owner HAgk14…, mint USDC → bump 254.
    CHECK(izan::crypto::sol_ata("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk",
              "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v")
        == "5N3f1tj9v1vc5TUZ8S7mCAnVmjVKrfnzXWhxLaxyZAgt");
    // A real public key sits ON the curve; its own bytes are exactly
    // what a PDA must never be.
    std::array<uint8_t, 32> pk {};
    std::size_t sz = pk.size();
    REQUIRE(b58tobin(
        pk.data(), &sz, "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk"));
    CHECK(izan::crypto::sol_on_curve(pk));
    CHECK_THROWS(izan::crypto::sol_ata("junk", "junk"));
}

TEST_CASE("the SPL transfer wears one shape and the table cannot be rerouted")
{
    const char* alice = "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk";
    const char* bob = "Hh8QwFUA6MtVu1qAoq12ucvFHNwCcVTV7hpWjeY1Hztb";
    const char* usdc = "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v";
    std::array<uint8_t, 32> hash {};
    hash.fill(0x33);
    const auto msg
        = izan::sol::encode_spl_transfer(alice, bob, usdc, 2500000, 6, hash);
    const auto back = izan::sol::parse_spl_transfer(msg);
    CHECK(back.owner == alice);
    CHECK(back.dest == bob);
    CHECK(back.mint == usdc);
    CHECK(back.amount == 2500000);
    CHECK(back.decimals == 6);
    CHECK(back.blockhash == hash);

    // Self-transfers are refused up front, not encoded misshapen.
    CHECK_THROWS(
        izan::sol::encode_spl_transfer(alice, alice, usdc, 1, 6, hash));
    CHECK_THROWS(izan::sol::encode_spl_transfer(alice, bob, usdc, 0, 6, hash));

    // The whitelist: reroute any account and the parser walks away.
    auto tamper = [&](std::size_t at, uint8_t v) {
        auto bad = msg;
        bad[at] ^= v;
        return bad;
    };
    // keys start at offset 4; flipping a byte inside src ATA (idx 1),
    // dst ATA (idx 2) or the token program (idx 6) must all refuse.
    CHECK_THROWS(izan::sol::parse_spl_transfer(tamper(4 + 32 + 5, 0x01)));
    CHECK_THROWS(izan::sol::parse_spl_transfer(tamper(4 + 64 + 5, 0x01)));
    CHECK_THROWS(izan::sol::parse_spl_transfer(tamper(4 + 192 + 5, 0x01)));
    // The System transfer parser must not accept this shape either.
    CHECK_THROWS(izan::sol::parse_transfer_message(msg));
    auto trailing = msg;
    trailing.push_back(0);
    CHECK_THROWS(izan::sol::parse_spl_transfer(trailing));
}

TEST_CASE("a Token-2022 transfer keeps its own program and its own doors")
{
    const char* alice = "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk";
    const char* bob = "Hh8QwFUA6MtVu1qAoq12ucvFHNwCcVTV7hpWjeY1Hztb";
    const char* pump = "3WjLscH2JsXLEFJZRA9z8ti8yRGxWGKbqymPd7UicRth";
    std::array<uint8_t, 32> hash {};
    hash.fill(0x44);
    const auto msg
        = izan::sol::encode_spl_transfer(alice, bob, pump, 777, 6, hash, true);
    const auto back = izan::sol::parse_spl_transfer(msg);
    CHECK(back.token2022);
    CHECK(back.mint == pump);
    CHECK(back.amount == 777);

    // The two program families never share an ATA: the same holding
    // encoded classic parses as classic yet lands elsewhere.
    const auto classic
        = izan::sol::encode_spl_transfer(alice, bob, pump, 777, 6, hash);
    CHECK(!izan::sol::parse_spl_transfer(classic).token2022);
    CHECK(izan::crypto::sol_ata(alice, pump, true)
        != izan::crypto::sol_ata(alice, pump));

    // Swapping ONLY the program key leaves the ATAs derived under the
    // other program — the duel must refuse the half-breed.
    auto half = msg;
    const auto classic_keys = std::span(classic).subspan(4 + 192, 32);
    std::copy(classic_keys.begin(), classic_keys.end(), half.begin() + 4 + 192);
    CHECK_THROWS(izan::sol::parse_spl_transfer(half));
}

TEST_CASE("a mint's on-chain card is found, read and defanged")
{
    // The Metaplex PDA, pinned to USDC's live metadata account
    // (owner and contents verified on mainnet 2026-07-19).
    CHECK(izan::crypto::sol_metadata_pda(
              "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v")
        == "5x38Kp4hvdomTCnCrAny4UtMUt5rQBdB6px2K1Ui45Wq");

    // Borsh body: key + two pubkeys, then NUL-padded strings.
    std::vector<uint8_t> data { 4 };
    data.insert(data.end(), 64, 0xaa);
    auto put_str = [&](std::string s, std::size_t padded) {
        s.resize(padded, '\0');
        data.push_back(uint8_t(padded));
        data.insert(data.end(), 3, 0);
        data.insert(data.end(), s.begin(), s.end());
    };
    put_str("USD Coin", 32);
    put_str("USDC", 10);
    const auto meta = izan::sol::parse_metaplex_meta(data);
    CHECK(meta.name == "USD Coin");
    CHECK(meta.symbol == "USDC");
    CHECK_THROWS(
        izan::sol::parse_metaplex_meta(std::span(data).subspan(0, 70)));

    // A Token-2022 answer, as mainnet spoke it for the WOC mint.
    const char* answer = R"({"context":{"slot":1},"value":{"data":{"parsed":{
        "info":{"decimals":6,"extensions":[
          {"extension":"metadataPointer","state":{"authority":null}},
          {"extension":"tokenMetadata","state":{"additionalMetadata":[],
            "mint":"3WjLscH2JsXLEFJZRA9z8ti8yRGxWGKbqymPd7UicRth",
            "name":"World Of Claudecraft","symbol":"WOC",
            "uri":"https://example.invalid"}}]},
        "type":"mint"},"program":"spl-token-2022"},
        "owner":"TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb"}})";
    const auto m22 = izan::sol::parse_token2022_meta(answer);
    CHECK(m22.name == "World Of Claudecraft");
    CHECK(m22.symbol == "WOC");

    // The defanger: controls, zero-width and bidi overrides vanish,
    // truncation never bisects a character.
    using izan::sol::sanitize_token_text;
    CHECK(sanitize_token_text("US‮DC​ ok\n", 32) == "USDC ok");
    CHECK(sanitize_token_text("  padded  ", 32) == "padded");
    CHECK(sanitize_token_text("日本語トークン", 8) == "日本");
    CHECK(sanitize_token_text("evil\xff\xfetail", 32) == "evil");
}
