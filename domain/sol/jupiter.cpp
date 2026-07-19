#include "domain/sol/jupiter.hpp"

#include <charconv>
#include <cstring>
#include <stdexcept>

#include <glaze/glaze.hpp>
#include <sodium.h>

#include "core/crypto/sol.hpp"
#include "domain/sol/sol_tx.hpp"
#include "domain/sol/solana.hpp"
#include "platform/net/http_client.hpp"

namespace izan::sol {

namespace {

    constexpr const char* kJupHost = "lite-api.jup.ag";

    uint64_t u64_field(const auto& obj, const char* key)
    {
        const auto it = obj.find(key);
        if (it == obj.end() || !it->second.is_string())
            throw std::runtime_error(
                std::string("jupiter: quote missing ") + key);
        const std::string& s = it->second.get_string();
        uint64_t v = 0;
        const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec != std::errc() || res.ptr != s.data() + s.size())
            throw std::runtime_error(std::string("jupiter: unreadable ") + key);
        return v;
    }

}

JupQuote parse_jup_quote(std::string_view json)
{
    glz::json_t doc;
    if (glz::read_json(doc, json) || !doc.is_object())
        throw std::runtime_error("jupiter: quote not an object");
    const auto& obj = doc.get_object();
    if (obj.contains("error"))
        throw std::runtime_error("jupiter: "
            + (obj.at("error").is_string() ? obj.at("error").get_string()
                                           : std::string("quote refused")));
    JupQuote q;
    q.quote_json = json;
    q.in_amount = u64_field(obj, "inAmount");
    q.out_amount = u64_field(obj, "outAmount");
    q.out_min = u64_field(obj, "otherAmountThreshold");
    const auto impact = obj.find("priceImpactPct");
    if (impact != obj.end() && impact->second.is_string())
        q.price_impact_pct = impact->second.get_string();
    const auto plan = obj.find("routePlan");
    if (plan != obj.end() && plan->second.is_array()
        && !plan->second.get_array().empty()) {
        const glz::json_t& leg = plan->second.get_array().front();
        if (leg.is_object()) {
            const auto info = leg.get_object().find("swapInfo");
            if (info != leg.get_object().end() && info->second.is_object()) {
                const auto label = info->second.get_object().find("label");
                if (label != info->second.get_object().end()
                    && label->second.is_string())
                    q.route_label = label->second.get_string();
            }
        }
    }
    return q;
}

JupQuote jup_quote(std::string_view input_mint, std::string_view output_mint,
    uint64_t amount, uint32_t slippage_bps)
{
    if (!valid_address(input_mint) || !valid_address(output_mint))
        throw std::invalid_argument("jupiter: not a solana mint");
    net::HttpsClient client(kJupHost, "443");
    const std::string target = "/swap/v1/quote?inputMint="
        + std::string(input_mint) + "&outputMint=" + std::string(output_mint)
        + "&amount=" + std::to_string(amount)
        + "&slippageBps=" + std::to_string(slippage_bps);
    const net::HttpResponse res
        = client.get(target, { { "Accept", "application/json" } });
    if (res.status != 200)
        throw std::runtime_error("jupiter: quote answered "
            + std::to_string(res.status) + ": " + res.body.substr(0, 200));
    return parse_jup_quote(res.body);
}

std::vector<uint8_t> parse_jup_swap(std::string_view json)
{
    glz::json_t doc;
    if (glz::read_json(doc, json) || !doc.is_object())
        throw std::runtime_error("jupiter: swap answer not an object");
    const auto& obj = doc.get_object();
    const auto tx = obj.find("swapTransaction");
    if (tx == obj.end() || !tx->second.is_string())
        throw std::runtime_error("jupiter: swap answer missing transaction");
    const std::string& b64 = tx->second.get_string();
    std::vector<uint8_t> out(b64.size());
    std::size_t len = 0;
    if (sodium_base642bin(out.data(), out.size(), b64.data(), b64.size(),
            nullptr, &len, nullptr, sodium_base64_VARIANT_ORIGINAL)
        != 0)
        throw std::runtime_error("jupiter: transaction not base64");
    out.resize(len);
    return out;
}

std::vector<uint8_t> jup_swap_tx(
    const JupQuote& quote, std::string_view user_pubkey)
{
    if (!valid_address(user_pubkey))
        throw std::invalid_argument("jupiter: not a solana address");
    net::HttpsClient client(kJupHost, "443");
    const std::string body = "{\"quoteResponse\":" + quote.quote_json
        + ",\"userPublicKey\":\"" + std::string(user_pubkey)
        + "\",\"wrapAndUnwrapSol\":true}";
    const net::HttpResponse res = client.post(
        "/swap/v1/swap", body, { { "Accept", "application/json" } });
    if (res.status != 200)
        throw std::runtime_error("jupiter: swap answered "
            + std::to_string(res.status) + ": " + res.body.substr(0, 200));
    return parse_jup_swap(res.body);
}

}
