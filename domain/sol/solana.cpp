#include "domain/sol/solana.hpp"

#include <charconv>
#include <cstring>
#include <map>
#include <mutex>
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
                + std::string(program) + "\"},{\"encoding\":\"jsonParsed\"}]"));
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
        throw std::invalid_argument(
            "not a solana address: " + std::string(mint));
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

std::string sanitize_token_text(std::string_view raw, std::size_t max_bytes)
{
    // Walk UTF-8, dropping what a spoofer leans on: controls,
    // zero-width joiners and the bidi-override family. Truncation
    // lands on a character boundary or not at all.
    std::string out;
    std::size_t i = 0;
    while (i < raw.size()) {
        const uint8_t lead = uint8_t(raw[i]);
        std::size_t len = lead < 0x80 ? 1
            : (lead >> 5) == 0x6      ? 2
            : (lead >> 4) == 0xe      ? 3
            : (lead >> 3) == 0x1e     ? 4
                                      : 0;
        if (len == 0 || i + len > raw.size())
            break; // malformed tail: keep what's clean so far
        uint32_t cp = len == 1 ? lead : lead & (0x7f >> len);
        for (std::size_t k = 1; k < len; ++k) {
            if ((uint8_t(raw[i + k]) & 0xc0) != 0x80)
                return out;
            cp = (cp << 6) | (uint8_t(raw[i + k]) & 0x3f);
        }
        const bool bad = cp < 0x20 || cp == 0x7f
            || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x202a && cp <= 0x202e)
            || (cp >= 0x2066 && cp <= 0x2069) || cp == 0xfeff;
        if (!bad) {
            if (out.size() + len > max_bytes)
                break;
            out.append(raw.substr(i, len));
        }
        i += len;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    std::size_t start = 0;
    while (start < out.size() && out[start] == ' ')
        ++start;
    return out.substr(start);
}

MintMeta parse_metaplex_meta(std::span<const uint8_t> data)
{
    // Borsh: key(1) update_authority(32) mint(32) then three
    // length-prefixed strings; Metaplex pads them with NULs inside
    // the declared length.
    std::size_t pos = 1 + 32 + 32;
    auto read_str = [&]() -> std::string {
        if (pos + 4 > data.size())
            throw std::runtime_error("sol: metadata truncated");
        uint32_t len = 0;
        for (int k = 3; k >= 0; --k)
            len = (len << 8) | data[pos + std::size_t(k)];
        pos += 4;
        if (len > 4096 || pos + len > data.size())
            throw std::runtime_error("sol: metadata truncated");
        std::string s(reinterpret_cast<const char*>(data.data() + pos), len);
        pos += len;
        if (const auto cut = s.find('\0'); cut != std::string::npos)
            s.resize(cut);
        return s;
    };
    if (data.size() < pos + 4)
        throw std::runtime_error("sol: metadata truncated");
    MintMeta m;
    m.name = read_str();
    m.symbol = read_str();
    return m;
}

MintMeta parse_token2022_meta(std::string_view json)
{
    glz::json_t doc;
    if (glz::read_json(doc, json) || !doc.is_object())
        throw std::runtime_error("sol: account info not an object");
    auto dig
        = [](const glz::json_t& node, const char* key) -> const glz::json_t* {
        if (!node.is_object())
            return nullptr;
        const auto it = node.get_object().find(key);
        return it == node.get_object().end() ? nullptr : &it->second;
    };
    const glz::json_t* value = dig(doc, "value");
    const glz::json_t* data = value ? dig(*value, "data") : nullptr;
    const glz::json_t* parsed = data ? dig(*data, "parsed") : nullptr;
    const glz::json_t* info = parsed ? dig(*parsed, "info") : nullptr;
    const glz::json_t* exts = info ? dig(*info, "extensions") : nullptr;
    if (!exts || !exts->is_array())
        throw std::runtime_error("sol: mint carries no extensions");
    for (const glz::json_t& e : exts->get_array()) {
        const glz::json_t* kind = dig(e, "extension");
        if (!kind || !kind->is_string()
            || kind->get_string() != "tokenMetadata")
            continue;
        const glz::json_t* state = dig(e, "state");
        const glz::json_t* name = state ? dig(*state, "name") : nullptr;
        const glz::json_t* sym = state ? dig(*state, "symbol") : nullptr;
        MintMeta m;
        if (name && name->is_string())
            m.name = name->get_string();
        if (sym && sym->is_string())
            m.symbol = sym->get_string();
        return m;
    }
    throw std::runtime_error("sol: mint carries no tokenMetadata");
}

MintMeta mint_meta(
    chains::RpcClient& rpc, std::string_view mint, bool token2022)
{
    if (!valid_address(mint))
        throw std::invalid_argument(
            "not a solana address: " + std::string(mint));
    static std::mutex cache_mx;
    static std::map<std::string, MintMeta, std::less<>> cache;
    {
        std::lock_guard lk(cache_mx);
        if (const auto it = cache.find(mint); it != cache.end())
            return it->second;
    }
    MintMeta m;
    if (token2022) {
        // Token-2022 keeps its card in its own pocket; the node's
        // jsonParsed decoder reads the TLV for us.
        m = parse_token2022_meta(rpc.call("getAccountInfo",
            "[\"" + std::string(mint) + "\",{\"encoding\":\"jsonParsed\"}]"));
    } else {
        const std::string pda = crypto::sol_metadata_pda(mint);
        const std::string res = rpc.call(
            "getAccountInfo", "[\"" + pda + "\",{\"encoding\":\"base64\"}]");
        glz::json_t doc;
        if (glz::read_json(doc, res) || !doc.is_object())
            throw std::runtime_error("sol: account info not an object");
        const auto& root = doc.get_object();
        const auto value = root.find("value");
        if (value == root.end() || !value->second.is_object())
            throw std::runtime_error("sol: mint has no metadata account");
        const auto& v = value->second.get_object();
        const auto arr = v.find("data");
        if (arr == v.end() || !arr->second.is_array()
            || arr->second.get_array().empty()
            || !arr->second.get_array().front().is_string())
            throw std::runtime_error("sol: metadata data unreadable");
        const std::string& b64 = arr->second.get_array().front().get_string();
        std::vector<uint8_t> bytes(b64.size());
        std::size_t len = 0;
        if (sodium_base642bin(bytes.data(), bytes.size(), b64.data(),
                b64.size(), nullptr, &len, nullptr,
                sodium_base64_VARIANT_ORIGINAL)
            != 0)
            throw std::runtime_error("sol: metadata not base64");
        bytes.resize(len);
        m = parse_metaplex_meta(bytes);
    }
    m.name = sanitize_token_text(m.name, 48);
    m.symbol = sanitize_token_text(m.symbol, 16);
    std::lock_guard lk(cache_mx);
    cache.emplace(std::string(mint), m);
    return m;
}

}
