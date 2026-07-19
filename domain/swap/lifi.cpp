#include "domain/swap/lifi.hpp"

#include <stdexcept>

#include <glaze/glaze.hpp>

namespace izan::swap {

namespace {

    const glz::json_t& member(const glz::json_t& obj, const char* name)
    {
        if (!obj.is_object())
            throw std::runtime_error("lifi: not an object");
        const auto& map = obj.get_object();
        const auto it = map.find(name);
        if (it == map.end())
            throw std::runtime_error(
                std::string("lifi: quote missing ") + name);
        return it->second;
    }

    std::string text(const glz::json_t& obj, const char* name)
    {
        const glz::json_t& v = member(obj, name);
        if (!v.is_string())
            throw std::runtime_error(
                std::string("lifi: quote field not a string: ") + name);
        return v.get_string();
    }

    uint8_t nibble(char c)
    {
        if (c >= '0' && c <= '9')
            return uint8_t(c - '0');
        if (c >= 'a' && c <= 'f')
            return uint8_t(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
            return uint8_t(c - 'A' + 10);
        throw std::runtime_error("lifi: bad hex in calldata");
    }

    std::vector<uint8_t> hex_bytes(std::string_view hex)
    {
        if (hex.starts_with("0x") || hex.starts_with("0X"))
            hex.remove_prefix(2);
        if (hex.size() % 2 != 0)
            throw std::runtime_error("lifi: odd calldata length");
        std::vector<uint8_t> out;
        out.reserve(hex.size() / 2);
        for (std::size_t i = 0; i < hex.size(); i += 2)
            out.push_back(uint8_t(nibble(hex[i]) << 4 | nibble(hex[i + 1])));
        return out;
    }

    uint64_t hex_u64(std::string_view hex)
    {
        if (hex.starts_with("0x") || hex.starts_with("0X"))
            hex.remove_prefix(2);
        if (hex.empty() || hex.size() > 16)
            throw std::runtime_error("lifi: gas limit out of range");
        uint64_t v = 0;
        for (const char c : hex)
            v = v << 4 | nibble(c);
        return v;
    }

}

SwapQuote parse_quote(std::string_view json)
{
    glz::json_t doc;
    if (glz::read_json(doc, json))
        throw std::runtime_error("lifi: quote is not JSON");

    const glz::json_t& estimate = member(doc, "estimate");
    const glz::json_t& request = member(doc, "transactionRequest");

    SwapQuote out;
    out.tool = text(doc, "tool");
    out.approval_address = text(estimate, "approvalAddress");
    out.from_amount = units::U256::from_dec(text(estimate, "fromAmount"));
    out.to_amount = units::U256::from_dec(text(estimate, "toAmount"));
    out.to_amount_min = units::U256::from_dec(text(estimate, "toAmountMin"));
    out.to = text(request, "to");
    out.data = hex_bytes(text(request, "data"));
    out.value = units::U256::from_hex(text(request, "value"));
    out.gas_limit = hex_u64(text(request, "gasLimit"));
    if (out.data.empty())
        throw std::runtime_error("lifi: quote carries no calldata");
    if (out.to_amount_min.is_zero())
        throw std::runtime_error("lifi: quote guarantees nothing");
    return out;
}

std::string quote_target(uint64_t chain_id, std::string_view from_token,
    std::string_view to_token, const units::U256& amount,
    std::string_view from_address, std::string_view integrator)
{
    std::string target = "/v1/quote?fromChain=" + std::to_string(chain_id)
        + "&toChain=" + std::to_string(chain_id);
    target += "&fromToken=";
    target += from_token;
    target += "&toToken=";
    target += to_token;
    target += "&fromAmount=" + amount.to_dec();
    target += "&fromAddress=";
    target += from_address;
    target += "&integrator=";
    target += integrator;
    return target;
}

SwapQuote fetch_quote(net::HttpsClient& client, uint64_t chain_id,
    std::string_view from_token, std::string_view to_token,
    const units::U256& amount, std::string_view from_address,
    std::string_view integrator)
{
    const net::HttpResponse res
        = client.get(quote_target(chain_id, from_token, to_token, amount,
                         from_address, integrator),
            { { "Accept", "application/json" } });
    if (res.status != 200) {
        // The aggregator answers errors in JSON; surface its message
        // when there is one — "no route" beats "http 404".
        glz::json_t err;
        if (!glz::read_json(err, res.body) && err.is_object()) {
            const auto& map = err.get_object();
            const auto it = map.find("message");
            if (it != map.end() && it->second.is_string())
                throw std::runtime_error("lifi: " + it->second.get_string());
        }
        throw std::runtime_error(
            "lifi: quote answered " + std::to_string(res.status));
    }
    return parse_quote(res.body);
}

}
