#include "domain/assets/history.hpp"

#include <algorithm>
#include <stdexcept>

#include <glaze/glaze.hpp>

#include "core/units/decimal.hpp"
#include "platform/net/http_client.hpp"

namespace izan::assets {

namespace {

    std::string lower_of(std::string_view s)
    {
        std::string out(s);
        std::ranges::transform(out, out.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        return out;
    }

    std::string field(const glz::json_t& row, const char* name)
    {
        const auto& obj = row.get_object();
        const auto it = obj.find(name);
        return it != obj.end() && it->second.is_string()
            ? it->second.get_string()
            : std::string();
    }

}

namespace {

    // The shared walk over an etherscan-style answer; tokens = true
    // additionally demands a readable token identity per row.
    std::vector<TxRecord> parse_rows(
        std::string_view json, std::string_view self, bool tokens)
    {
        glz::json_t doc;
        if (glz::read_json(doc, json) || !doc.is_object())
            throw std::runtime_error("txlist: not a JSON object");
        const auto& top = doc.get_object();
        const auto result = top.find("result");
        if (result == top.end())
            throw std::runtime_error("txlist: no result");
        // status "0" covers both real errors and the perfectly fine
        // "No transactions found"; an empty or non-array result is
        // just an empty ledger.
        if (!result->second.is_array())
            return {};

        const std::string me = lower_of(self);
        std::vector<TxRecord> out;
        for (const glz::json_t& row : result->second.get_array()) {
            if (!row.is_object())
                continue;
            TxRecord rec;
            rec.hash = field(row, "hash");
            if (rec.hash.empty())
                continue;
            const std::string from = lower_of(field(row, "from"));
            const std::string to = lower_of(field(row, "to"));
            rec.incoming = to == me && from != me;
            rec.counterparty
                = rec.incoming ? field(row, "from") : field(row, "to");
            try {
                rec.value = units::U256::from_dec(field(row, "value"));
            } catch (const std::exception&) {
                continue; // a row without a readable value is no row
            }
            const std::string when = field(row, "timeStamp");
            rec.time
                = when.empty() ? 0 : std::strtoull(when.c_str(), nullptr, 10);
            if (tokens) {
                rec.token_symbol = field(row, "tokenSymbol");
                const std::string dec = field(row, "tokenDecimal");
                if (rec.token_symbol.empty() || dec.empty())
                    continue; // a transfer of an unnameable thing
                rec.token_decimals
                    = unsigned(std::strtoul(dec.c_str(), nullptr, 10));
                if (rec.token_decimals > units::kMaxDecimals)
                    continue;
            } else {
                rec.failed = field(row, "isError") == "1";
            }
            out.push_back(std::move(rec));
        }
        return out;
    }

}

std::vector<TxRecord> parse_txlist(std::string_view json, std::string_view self)
{
    return parse_rows(json, self, false);
}

std::vector<TxRecord> parse_tokentx(
    std::string_view json, std::string_view self)
{
    return parse_rows(json, self, true);
}

namespace {

    std::string fetch_page(const chains::ChainSpec& chain,
        const std::string& action, const std::string& address)
    {
        const net::HttpsUrl base = net::parse_https_url(chain.history);
        net::HttpsClient client(base.host, base.port);
        const std::string target = "/api?module=account&action=" + action
            + "&address=" + address + "&sort=desc&page=1&offset=25";
        const net::HttpResponse res
            = client.get(target, { { "Accept", "application/json" } });
        if (res.status != 200)
            throw std::runtime_error(
                chain.name + " history answered " + std::to_string(res.status));
        return res.body;
    }

}

std::vector<TxRecord> fetch_history(
    const chains::ChainSpec& chain, const std::string& address)
{
    if (chain.history.empty())
        return {};
    return parse_txlist(fetch_page(chain, "txlist", address), address);
}

std::vector<TxRecord> fetch_token_history(
    const chains::ChainSpec& chain, const std::string& address)
{
    if (chain.history.empty())
        return {};
    return parse_tokentx(fetch_page(chain, "tokentx", address), address);
}

}
