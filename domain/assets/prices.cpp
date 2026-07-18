#include "domain/assets/prices.hpp"

#include <stdexcept>

#include <glaze/glaze.hpp>

#include "platform/net/http_client.hpp"

namespace izan::assets {

std::string coingecko_id(std::string_view symbol)
{
    // The shipped default assets, by hand. A symbol is not a global
    // identity — this table is the curated bridge, and growing it is a
    // config-review event, not a string heuristic.
    static const std::unordered_map<std::string_view, std::string_view> kIds
        = { { "ETH", "ethereum" }, { "WETH", "weth" }, { "USDC", "usd-coin" },
              { "USDC.e", "usd-coin" }, { "USDT", "tether" }, { "DAI", "dai" },
              { "POL", "polygon-ecosystem-token" },
              { "MATIC", "matic-network" }, { "SOL", "solana" },
              { "BTC", "bitcoin" } };
    const auto it = kIds.find(symbol);
    return it == kIds.end() ? std::string() : std::string(it->second);
}

std::unordered_map<std::string, double> parse_usd_prices(std::string_view json)
{
    glz::json_t doc;
    if (glz::read_json(doc, json))
        throw std::runtime_error("price response is not JSON");
    if (!doc.is_object())
        throw std::runtime_error("price response is not an object");
    std::unordered_map<std::string, double> out;
    for (const auto& [id, entry] : doc.get_object()) {
        if (!entry.is_object())
            continue;
        const auto& fields = entry.get_object();
        const auto usd = fields.find("usd");
        if (usd != fields.end() && usd->second.is_number())
            out[id] = usd->second.get_number();
    }
    return out;
}

std::unordered_map<std::string, double> fetch_usd_prices(
    const std::vector<std::string>& ids)
{
    if (ids.empty())
        return {};
    std::string joined;
    for (const std::string& id : ids) {
        if (!joined.empty())
            joined += ',';
        joined += id;
    }
    net::HttpsClient client("api.coingecko.com");
    const net::HttpResponse res = client.get(
        "/api/v3/simple/price?ids=" + joined + "&vs_currencies=usd",
        { { "Accept", "application/json" } });
    if (res.status != 200)
        throw std::runtime_error(
            "price endpoint answered " + std::to_string(res.status));
    return parse_usd_prices(res.body);
}

}
