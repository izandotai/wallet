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

    // Phishing airdrops advertise inside the token identity itself —
    // a "symbol" carrying a sentence and a claim-site URL, pushed at
    // every active address. A real ticker is short and has neither
    // whitespace nor a web address; anything else is an ad, not an
    // asset, and has no place in the ledger.
    bool spammy_symbol(const std::string& symbol)
    {
        if (symbol.size() > 20)
            return true;
        for (const unsigned char c : symbol)
            if (c <= ' ')
                return true;
        const std::string low = lower_of(symbol);
        return low.contains("http") || low.contains("www.");
    }

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
                if (spammy_symbol(rec.token_symbol))
                    continue;
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

    std::string fetch_page(net::HttpsClient& client,
        const chains::ChainSpec& chain, const std::string& action,
        const std::string& address, int page)
    {
        const std::string target = "/api?module=account&action=" + action
            + "&address=" + address + "&sort=desc&page=" + std::to_string(page)
            + "&offset=25";
        const net::HttpResponse res
            = client.get(target, { { "Accept", "application/json" } });
        if (res.status != 200)
            throw std::runtime_error(
                chain.name + " history answered " + std::to_string(res.status));
        return res.body;
    }

}

std::vector<TxRecord> fetch_history(
    const chains::ChainSpec& chain, const std::string& address, int page)
{
    if (chain.history.empty())
        return {};
    const net::HttpsUrl base = net::parse_https_url(chain.history);
    net::HttpsClient client(base.host, base.port);
    return parse_txlist(
        fetch_page(client, chain, "txlist", address, page), address);
}

std::vector<TxRecord> fetch_token_history(
    const chains::ChainSpec& chain, const std::string& address, int page)
{
    if (chain.history.empty())
        return {};
    const net::HttpsUrl base = net::parse_https_url(chain.history);
    net::HttpsClient client(base.host, base.port);
    return parse_tokentx(
        fetch_page(client, chain, "tokentx", address, page), address);
}

Ledger fetch_ledger(
    const chains::ChainSpec& chain, const std::string& address, int page)
{
    Ledger out;
    if (chain.history.empty())
        return out;
    const net::HttpsUrl base = net::parse_https_url(chain.history);
    net::HttpsClient client(base.host, base.port);
    out.tokens = parse_tokentx(
        fetch_page(client, chain, "tokentx", address, page), address);
    out.native = parse_txlist(
        fetch_page(client, chain, "txlist", address, page), address);
    return out;
}

}
