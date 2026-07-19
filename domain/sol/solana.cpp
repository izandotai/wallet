#include "domain/sol/solana.hpp"

#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>

#include <glaze/glaze.hpp>
#include <sodium.h>

#include "core/crypto/sol.hpp"

extern "C" {
#include <base58.h>
}

namespace izan::sol {

bool valid_address(std::string_view text)
{
    return crypto::valid_sol_address(text);
}

units::U256 parse_balance_result(std::string_view result_json)
{
    glz::json_t doc;
    if (glz::read_json(doc, result_json) || !doc.is_object())
        throw std::runtime_error("sol: getBalance result not an object");
    const auto& obj = doc.get_object();
    const auto it = obj.find("value");
    if (it == obj.end() || !it->second.is_number())
        throw std::runtime_error("sol: getBalance result missing value");
    const double v = it->second.get_number();
    if (v < 0)
        throw std::runtime_error("sol: negative balance");
    // Lamport totals fit a double exactly up to 2^53 — about nine
    // billion SOL, versus a supply near six hundred million.
    return units::U256::from_u64(uint64_t(v));
}

units::U256 native_balance(chains::RpcClient& rpc, std::string_view address)
{
    if (!valid_address(address))
        throw std::invalid_argument(
            "not a solana address: " + std::string(address));
    return parse_balance_result(
        rpc.call("getBalance", "[\"" + std::string(address) + "\"]"));
}

std::vector<SolSig> parse_signatures(std::string_view result_json)
{
    glz::json_t doc;
    if (glz::read_json(doc, result_json) || !doc.is_array())
        throw std::runtime_error(
            "sol: getSignaturesForAddress result not an array");
    std::vector<SolSig> out;
    for (const glz::json_t& entry : doc.get_array()) {
        if (!entry.is_object())
            continue;
        const auto& obj = entry.get_object();
        const auto sig = obj.find("signature");
        if (sig == obj.end() || !sig->second.is_string())
            continue;
        SolSig rec;
        rec.signature = sig->second.get_string();
        const auto when = obj.find("blockTime");
        if (when != obj.end() && when->second.is_number()
            && when->second.get_number() > 0)
            rec.time = uint64_t(when->second.get_number());
        const auto err = obj.find("err");
        rec.failed = err != obj.end() && !err->second.is_null();
        out.push_back(std::move(rec));
    }
    return out;
}

std::vector<SolSig> recent_signatures(
    chains::RpcClient& rpc, std::string_view address)
{
    if (!valid_address(address))
        throw std::invalid_argument(
            "not a solana address: " + std::string(address));
    return parse_signatures(rpc.call("getSignaturesForAddress",
        "[\"" + std::string(address) + "\",{\"limit\":25}]"));
}

}

namespace izan::sol {

std::array<uint8_t, 32> parse_blockhash_result(std::string_view result_json)
{
    glz::json_t doc;
    if (glz::read_json(doc, result_json) || !doc.is_object())
        throw std::runtime_error("sol: getLatestBlockhash not an object");
    const auto& obj = doc.get_object();
    const auto value = obj.find("value");
    if (value == obj.end() || !value->second.is_object())
        throw std::runtime_error("sol: blockhash answer missing value");
    const auto& v = value->second.get_object();
    const auto hash = v.find("blockhash");
    if (hash == v.end() || !hash->second.is_string())
        throw std::runtime_error("sol: blockhash answer missing blockhash");
    std::array<uint8_t, 32> out {};
    std::size_t sz = out.size();
    if (!b58tobin(out.data(), &sz, hash->second.get_string().c_str())
        || sz != out.size())
        throw std::runtime_error("sol: blockhash not 32 bytes of base58");
    return out;
}

std::array<uint8_t, 32> latest_blockhash(chains::RpcClient& rpc)
{
    return parse_blockhash_result(
        rpc.call("getLatestBlockhash", "[{\"commitment\":\"finalized\"}]"));
}

std::string send_transaction(
    chains::RpcClient& rpc, std::span<const uint8_t> tx)
{
    if (tx.empty())
        throw std::invalid_argument("sol: empty transaction");
    std::string b64(
        sodium_base64_ENCODED_LEN(tx.size(), sodium_base64_VARIANT_ORIGINAL),
        '\0');
    sodium_bin2base64(b64.data(), b64.size(), tx.data(), tx.size(),
        sodium_base64_VARIANT_ORIGINAL);
    b64.resize(std::strlen(b64.c_str()));
    const std::string answer = rpc.call(
        "sendTransaction", "[\"" + b64 + "\",{\"encoding\":\"base64\"}]");
    glz::json_t doc;
    if (glz::read_json(doc, answer) || !doc.is_string())
        throw std::runtime_error("sol: sendTransaction answered no signature");
    return doc.get_string();
}

SigStatus parse_signature_status(std::string_view result_json)
{
    glz::json_t doc;
    if (glz::read_json(doc, result_json) || !doc.is_object())
        throw std::runtime_error("sol: getSignatureStatuses not an object");
    const auto& obj = doc.get_object();
    const auto value = obj.find("value");
    if (value == obj.end() || !value->second.is_array()
        || value->second.get_array().empty())
        throw std::runtime_error("sol: status answer missing value");
    const glz::json_t& entry = value->second.get_array().front();
    if (entry.is_null())
        return SigStatus::Unknown;
    if (!entry.is_object())
        throw std::runtime_error("sol: status entry malformed");
    const auto& st = entry.get_object();
    const auto err = st.find("err");
    if (err != st.end() && !err->second.is_null())
        return SigStatus::Failed;
    const auto level = st.find("confirmationStatus");
    if (level != st.end() && level->second.is_string()) {
        const std::string& s = level->second.get_string();
        if (s == "finalized")
            return SigStatus::Finalized;
        if (s == "confirmed")
            return SigStatus::Confirmed;
    }
    return SigStatus::Processed;
}

SigStatus signature_status(chains::RpcClient& rpc, std::string_view signature)
{
    return parse_signature_status(rpc.call(
        "getSignatureStatuses", "[[\"" + std::string(signature) + "\"]]"));
}

uint64_t rent_exempt_minimum(chains::RpcClient& rpc, uint64_t size)
{
    const std::string answer = rpc.call(
        "getMinimumBalanceForRentExemption", "[" + std::to_string(size) + "]");
    glz::json_t doc;
    if (glz::read_json(doc, answer) || !doc.is_number() || doc.get_number() < 0)
        throw std::runtime_error("sol: rent minimum unreadable");
    return uint64_t(doc.get_number());
}

}

namespace izan::sol {

std::vector<SplHolding> parse_token_accounts(std::string_view result_json)
{
    glz::json_t doc;
    if (glz::read_json(doc, result_json) || !doc.is_object())
        throw std::runtime_error("sol: token accounts not an object");
    const auto& root = doc.get_object();
    const auto value = root.find("value");
    if (value == root.end() || !value->second.is_array())
        throw std::runtime_error("sol: token accounts missing value");
    std::vector<SplHolding> out;
    for (const glz::json_t& entry : value->second.get_array()) {
        if (!entry.is_object())
            continue;
        const auto& e = entry.get_object();
        const auto pubkey = e.find("pubkey");
        const auto account = e.find("account");
        if (pubkey == e.end() || !pubkey->second.is_string()
            || account == e.end() || !account->second.is_object())
            continue;
        // account.data.parsed.info.{mint, tokenAmount{amount, decimals}}
        auto dig = [](const glz::json_t& node,
                       const char* key) -> const glz::json_t* {
            if (!node.is_object())
                return nullptr;
            const auto it = node.get_object().find(key);
            return it == node.get_object().end() ? nullptr : &it->second;
        };
        const glz::json_t* data = dig(account->second, "data");
        const glz::json_t* parsed = data ? dig(*data, "parsed") : nullptr;
        const glz::json_t* info = parsed ? dig(*parsed, "info") : nullptr;
        const glz::json_t* mint = info ? dig(*info, "mint") : nullptr;
        const glz::json_t* ta = info ? dig(*info, "tokenAmount") : nullptr;
        const glz::json_t* amount = ta ? dig(*ta, "amount") : nullptr;
        const glz::json_t* decimals = ta ? dig(*ta, "decimals") : nullptr;
        if (!mint || !mint->is_string() || !amount || !amount->is_string()
            || !decimals || !decimals->is_number())
            continue;
        SplHolding h;
        h.account = pubkey->second.get_string();
        h.mint = mint->get_string();
        const std::string& amt = amount->get_string();
        const auto res
            = std::from_chars(amt.data(), amt.data() + amt.size(), h.amount);
        if (res.ec != std::errc() || res.ptr != amt.data() + amt.size())
            continue;
        const double d = decimals->get_number();
        if (d < 0 || d > 255)
            continue;
        h.decimals = uint8_t(d);
        out.push_back(std::move(h));
    }
    return out;
}

std::vector<SplHolding> token_accounts(
    chains::RpcClient& rpc, std::string_view owner)
{
    if (!valid_address(owner))
        throw std::invalid_argument(
            "not a solana address: " + std::string(owner));
    auto ask = [&](const char* program) {
        return parse_token_accounts(rpc.call("getTokenAccountsByOwner",
            "[\"" + std::string(owner) + "\",{\"programId\":\""
                + std::string(program)
                + "\"},{\"encoding\":\"jsonParsed\"}]"));
    };
    std::vector<SplHolding> out
        = ask("TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA");
    std::vector<SplHolding> extra
        = ask("TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb");
    for (SplHolding& h : extra) {
        h.token2022 = true;
        out.push_back(std::move(h));
    }
    return out;
}

bool mint_is_token2022(chains::RpcClient& rpc, std::string_view mint)
{
    if (!valid_address(mint))
        throw std::invalid_argument("not a solana address: " + std::string(mint));
    const std::string res = rpc.call("getAccountInfo",
        "[\"" + std::string(mint) + "\",{\"encoding\":\"base64\"}]");
    glz::json_t doc;
    if (glz::read_json(doc, res) || !doc.is_object())
        throw std::runtime_error("sol: account info not an object");
    const auto& root = doc.get_object();
    const auto value = root.find("value");
    if (value == root.end() || !value->second.is_object())
        throw std::runtime_error("sol: no such mint on-chain");
    const auto& v = value->second.get_object();
    const auto owner = v.find("owner");
    if (owner == v.end() || !owner->second.is_string())
        throw std::runtime_error("sol: mint account without an owner");
    const std::string& program = owner->second.get_string();
    if (program == "TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb")
        return true;
    if (program == "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA")
        return false;
    throw std::runtime_error("sol: not a token mint (owner " + program + ")");
}

std::string known_mint_symbol(std::string_view mint)
{
    // The majors people actually hold; strangers show their address.
    if (mint == "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v")
        return "USDC";
    if (mint == "Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB")
        return "USDT";
    if (mint == "So11111111111111111111111111111111111111112")
        return "WSOL";
    if (mint == "JUPyiwrYJFskUPiHa7hkeR8VUtAeFoSYbKedZNsDvCN")
        return "JUP";
    if (mint == "DezXAZ8z7PnrnRJjz3wXBoRgixCa6xjnB7YaB1pPB263")
        return "Bonk";
    return {};
}

}

namespace izan::sol {

bool account_exists(chains::RpcClient& rpc, std::string_view address)
{
    if (!valid_address(address))
        throw std::invalid_argument(
            "not a solana address: " + std::string(address));
    const std::string answer = rpc.call("getAccountInfo",
        "[\"" + std::string(address) + "\",{\"encoding\":\"base64\"}]");
    glz::json_t doc;
    if (glz::read_json(doc, answer) || !doc.is_object())
        throw std::runtime_error("sol: getAccountInfo not an object");
    const auto& obj = doc.get_object();
    const auto value = obj.find("value");
    return value != obj.end() && !value->second.is_null();
}

}

namespace izan::sol {

uint8_t mint_decimals(chains::RpcClient& rpc, std::string_view mint)
{
    if (!valid_address(mint))
        throw std::invalid_argument(
            "not a solana address: " + std::string(mint));
    const std::string answer
        = rpc.call("getTokenSupply", "[\"" + std::string(mint) + "\"]");
    glz::json_t doc;
    if (glz::read_json(doc, answer) || !doc.is_object())
        throw std::runtime_error("sol: getTokenSupply not an object");
    const auto& obj = doc.get_object();
    const auto value = obj.find("value");
    if (value == obj.end() || !value->second.is_object())
        throw std::runtime_error("sol: token supply missing value");
    const auto dec = value->second.get_object().find("decimals");
    if (dec == value->second.get_object().end() || !dec->second.is_number()
        || dec->second.get_number() < 0 || dec->second.get_number() > 255)
        throw std::runtime_error("sol: token decimals unreadable");
    return uint8_t(dec->second.get_number());
}

}
