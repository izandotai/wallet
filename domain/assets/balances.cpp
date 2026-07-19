#include "domain/assets/balances.hpp"

#include <stdexcept>
#include <string>

#include "core/codec/abi.hpp"
#include "core/crypto/eth.hpp"

namespace izan::assets {

namespace {

    // Checksummed 0x form, or a throw — never let a typo'd address
    // reach the wire and come back as a plausible zero balance.
    std::string require_address(std::string_view address)
    {
        std::string checked = crypto::eth_checksum_address(address);
        if (checked.empty())
            throw std::invalid_argument(
                "not an address: " + std::string(address));
        return checked;
    }

}

units::U256 native_balance(chains::RpcClient& rpc, std::string_view address)
{
    const std::string addr = require_address(address);
    return units::U256::from_hex(
        rpc.call_string("eth_getBalance", "[\"" + addr + "\",\"latest\"]"));
}

units::U256 erc20_balance(
    chains::RpcClient& rpc, std::string_view token, std::string_view holder)
{
    const std::string contract = require_address(token);
    const std::string data = codec::CallData("balanceOf(address)")
                                 .add_address(require_address(holder))
                                 .to_hex();
    return codec::decode_u256(rpc.call_string("eth_call",
        "[{\"to\":\"" + contract + "\",\"data\":\"" + data
            + "\"},\"latest\"]"));
}

units::U256 erc20_allowance(chains::RpcClient& rpc, std::string_view token,
    std::string_view owner, std::string_view spender)
{
    const std::string contract = require_address(token);
    const std::string data = codec::CallData("allowance(address,address)")
                                 .add_address(require_address(owner))
                                 .add_address(require_address(spender))
                                 .to_hex();
    return codec::decode_u256(rpc.call_string("eth_call",
        "[{\"to\":\"" + contract + "\",\"data\":\"" + data
            + "\"},\"latest\"]"));
}

std::vector<uint8_t> erc20_approve_calldata(
    std::string_view spender, const units::U256& amount)
{
    return codec::CallData("approve(address,uint256)")
        .add_address(require_address(spender))
        .add_u256(amount)
        .to_bytes();
}

TokenProbe probe_token(chains::RpcClient& rpc, std::string_view token)
{
    const std::string contract = require_address(token);
    auto read = [&](const char* signature) {
        const std::string data = codec::CallData(signature).to_hex();
        return rpc.call_string("eth_call",
            "[{\"to\":\"" + contract + "\",\"data\":\"" + data
                + "\"},\"latest\"]");
    };

    TokenProbe out;
    out.symbol = codec::decode_abi_string(read("symbol()"));
    if (out.symbol.empty() || out.symbol.size() > 32)
        throw std::runtime_error("token symbol unreadable");
    const units::U256 dec = codec::decode_u256(read("decimals()"));
    const std::string dec_str = dec.to_dec();
    if (dec_str.size() > 2 || dec_str.empty())
        throw std::runtime_error("token decimals out of range");
    const int dec_val = std::stoi(dec_str);
    if (dec_val > 77)
        throw std::runtime_error("token decimals out of range");
    out.decimals = uint8_t(dec_val);
    return out;
}

}
