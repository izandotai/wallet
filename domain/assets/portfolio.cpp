#include "domain/assets/portfolio.hpp"

#include <stdexcept>

#include "core/crypto/eth.hpp"
#include "domain/assets/balances.hpp"

namespace izan::assets {

PortfolioReader::PortfolioReader(
    chains::ChainRegistry chains, TokenRegistry tokens)
    : m_chains(std::move(chains))
    , m_tokens(std::move(tokens))
{
    for (const TokenSpec& t : m_tokens.all())
        if (!m_chains.by_id(t.chain_id))
            throw std::runtime_error("portfolio: token " + t.symbol + " ("
                + t.address + ") references unknown chain "
                + std::to_string(t.chain_id));
}

std::vector<Holding> PortfolioReader::snapshot(std::string_view address)
{
    const std::string addr = crypto::eth_checksum_address(address);
    if (addr.empty())
        throw std::invalid_argument("not an address: " + std::string(address));

    std::vector<Holding> rows;
    for (const chains::ChainSpec& chain : m_chains.all()) {
        auto& client = m_clients[chain.chain_id];
        if (!client)
            client = std::make_unique<chains::RpcClient>(chain);

        Holding native { .chain_id = chain.chain_id,
            .chain = chain.name,
            .symbol = chain.symbol,
            .decimals = chain.decimals };
        try {
            native.amount = native_balance(*client, addr);
            native.ok = true;
        } catch (const std::exception& e) {
            native.error = e.what();
        }
        rows.push_back(std::move(native));

        for (const TokenSpec* token : m_tokens.tokens_for(chain.chain_id)) {
            Holding row { .chain_id = chain.chain_id,
                .chain = chain.name,
                .symbol = token->symbol,
                .token = token->address,
                .decimals = token->decimals };
            try {
                row.amount = erc20_balance(*client, token->address, addr);
                row.ok = true;
            } catch (const std::exception& e) {
                row.error = e.what();
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

}
