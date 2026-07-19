#include "core/crypto/secret_import.hpp"

#include <cstdint>
#include <optional>

#include "core/crypto/bip39.hpp"
#include "core/crypto/btc.hpp"
#include "core/crypto/eth.hpp"
// btc/sol address validators live with their curves
#include "core/crypto/sol.hpp"

namespace izan::crypto {

namespace {

    std::string_view trimmed(std::string_view text)
    {
        const auto a = text.find_first_not_of(" \t\r\n");
        if (a == std::string_view::npos)
            return {};
        const auto b = text.find_last_not_of(" \t\r\n");
        return text.substr(a, b - a + 1);
    }

    // A raw secp256k1 key pasted as hex: optional 0x, then exactly 64
    // hex digits, and not the zero scalar. (Whether it is a USABLE key
    // is proven later by actually signing with it before the wallet is
    // allowed to exist.)
    std::optional<secure::SecureBytes> parse_raw_key(std::string_view hex)
    {
        if (hex.starts_with("0x") || hex.starts_with("0X"))
            hex.remove_prefix(2);
        if (hex.size() != 64)
            return std::nullopt;
        secure::SecureBytes key(32);
        bool nonzero = false;
        for (int i = 0; i < 32; ++i) {
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            const int hi = nib(hex[std::size_t(2 * i)]);
            const int lo = nib(hex[std::size_t(2 * i + 1)]);
            if (hi < 0 || lo < 0)
                return std::nullopt;
            key.data()[i] = uint8_t(hi << 4 | lo);
            nonzero = nonzero || key.data()[i];
        }
        if (!nonzero)
            return std::nullopt;
        return key;
    }

}

DetectedSecret detect_secret(std::string_view text)
{
    const std::string_view body = trimmed(text);
    DetectedSecret out;
    if (body.empty())
        return out;
    if (mnemonic_valid(body)) {
        out.kind = SecretKind::Mnemonic;
    } else if (auto key = parse_raw_key(body)) {
        out.kind = SecretKind::RawKey;
        out.key = std::move(*key);
    } else if (auto key = wif_to_key(body)) {
        out.kind = SecretKind::Wif;
        out.key = std::move(*key);
    } else if (auto key = sol_key_from_base58(body)) {
        out.kind = SecretKind::SolKey;
        out.key = std::move(*key);
    } else if (!eth_checksum_address(body).empty()) {
        // A bare address is a wallet to watch, not a wallet to spend
        // from — no key rides along. One kind per family; the order
        // is safe because the encodings cannot collide (0x-hex,
        // base58check/bech32, bare base58 of 32 bytes).
        out.kind = SecretKind::EthAddress;
    } else if (valid_btc_address(body)) {
        out.kind = SecretKind::BtcAddress;
    } else if (valid_sol_address(body)) {
        out.kind = SecretKind::SolAddress;
    }
    return out;
}

}
