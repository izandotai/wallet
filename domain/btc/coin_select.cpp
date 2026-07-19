#include "domain/btc/coin_select.hpp"

#include <algorithm>
#include <stdexcept>

#include <glaze/glaze.hpp>

#include "core/crypto/btc.hpp"
#include "platform/net/http_client.hpp"

namespace izan::btc {

std::vector<Utxo> parse_utxos(std::string_view json)
{
    glz::json_t doc;
    if (glz::read_json(doc, json) || !doc.is_array())
        throw std::runtime_error("btc: utxo answer not an array");
    std::vector<Utxo> out;
    for (const glz::json_t& u : doc.get_array()) {
        if (!u.is_object())
            continue;
        const auto& obj = u.get_object();
        const auto txid = obj.find("txid");
        const auto vout = obj.find("vout");
        const auto value = obj.find("value");
        const auto status = obj.find("status");
        if (txid == obj.end() || !txid->second.is_string() || vout == obj.end()
            || !vout->second.is_number() || value == obj.end()
            || !value->second.is_number())
            continue;
        if (status != obj.end() && status->second.is_object()) {
            const auto& st = status->second.get_object();
            const auto conf = st.find("confirmed");
            if (conf == st.end() || !conf->second.is_boolean()
                || !conf->second.get_boolean())
                continue; // mempool money is not money yet
        }
        out.push_back(
            { txid->second.get_string(), uint32_t(vout->second.get_number()),
                uint64_t(value->second.get_number()) });
    }
    return out;
}

std::vector<Utxo> fetch_utxos(
    const chains::ChainSpec& spec, std::string_view address)
{
    if (!crypto::valid_btc_address(address))
        throw std::invalid_argument(
            "not a bitcoin address: " + std::string(address));
    std::string last_error = "no esplora endpoint configured";
    for (const std::string& base : spec.rpc) {
        try {
            const net::HttpsUrl url = net::parse_https_url(base);
            net::HttpsClient client(url.host, url.port);
            const std::string base_path
                = url.target == "/" ? std::string() : url.target;
            const net::HttpResponse res = client.get(
                base_path + "/address/" + std::string(address) + "/utxo",
                { { "Accept", "application/json" } });
            if (res.status != 200)
                throw std::runtime_error(
                    "esplora answered " + std::to_string(res.status));
            return parse_utxos(res.body);
        } catch (const std::exception& e) {
            last_error = e.what();
        }
    }
    throw std::runtime_error("btc: " + last_error);
}

uint64_t p2wpkh_vsize(std::size_t inputs, std::size_t outputs)
{
    // Weight: 4×(non-witness) + witness. Non-witness per input 41 B,
    // per P2WPKH output 31 B, frame 10 B + segwit marker pair; the
    // witness (sig + pubkey) adds ~27 vB per input. 68 vB per input
    // and 31 per output, 11 of frame — the customary safe rounding.
    return 11 + 68 * uint64_t(inputs) + 31 * uint64_t(outputs);
}

CoinSelection select_coins(
    std::vector<Utxo> utxos, uint64_t amount, uint64_t feerate_sat_vb)
{
    if (amount == 0)
        throw std::invalid_argument("btc: zero amount");
    if (feerate_sat_vb == 0)
        feerate_sat_vb = 1;
    std::sort(utxos.begin(), utxos.end(), [](const Utxo& a, const Utxo& b) {
        // Value first; txid:vout as the determinism tiebreak.
        if (a.value != b.value)
            return a.value > b.value;
        if (a.txid != b.txid)
            return a.txid < b.txid;
        return a.vout < b.vout;
    });
    CoinSelection sel;
    uint64_t gathered = 0;
    for (const Utxo& u : utxos) {
        sel.inputs.push_back(u);
        gathered += u.value;
        // Assume a change output while probing; the no-change case is
        // settled below, where it can only lower the bar.
        const uint64_t fee
            = p2wpkh_vsize(sel.inputs.size(), 2) * feerate_sat_vb;
        if (gathered >= amount + fee) {
            const uint64_t change = gathered - amount - fee;
            if (change < kDustSats) {
                // Dust folds into the fee, and the transaction slims
                // to one output — recompute honestly.
                sel.fee = gathered - amount;
                sel.change = 0;
            } else {
                sel.fee = fee;
                sel.change = change;
            }
            return sel;
        }
    }
    throw std::runtime_error("btc: coins cannot cover amount plus fee");
}

CoinSelection sweep_coins(std::vector<Utxo> utxos, uint64_t feerate_sat_vb)
{
    if (utxos.empty())
        throw std::runtime_error("btc: nothing to sweep");
    if (feerate_sat_vb == 0)
        feerate_sat_vb = 1;
    CoinSelection sel;
    sel.inputs = std::move(utxos);
    uint64_t total = 0;
    for (const Utxo& u : sel.inputs)
        total += u.value;
    sel.fee = p2wpkh_vsize(sel.inputs.size(), 1) * feerate_sat_vb;
    if (total <= sel.fee + kDustSats)
        throw std::runtime_error("btc: the fee would eat the sweep");
    sel.change = 0;
    return sel;
}

}
