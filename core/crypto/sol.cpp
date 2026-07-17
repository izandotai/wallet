#include "core/crypto/sol.hpp"

#include <algorithm>

#include "core/crypto/detail/path.hpp"

extern "C" {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include <bip32.h>
#pragma GCC diagnostic pop
#include <base58.h>
#include <curves.h>
#include <memzero.h>
}

namespace izan::crypto {

std::optional<SolKey> sol_derive(
    std::span<const uint8_t> seed, std::string_view path)
{
    HDNode node;
    if (hdnode_from_seed(
            seed.data(), static_cast<int>(seed.size()), ED25519_NAME, &node)
        == 0)
        return std::nullopt;
    const bool ok = detail::walk_path(path, [&](uint32_t index) {
        // ed25519 has no normal derivation; the spec only defines hardened.
        return (index & detail::kHardened) != 0
            && hdnode_private_ckd(&node, index) != 0;
    });
    if (!ok) {
        memzero(&node, sizeof node);
        return std::nullopt;
    }
    hdnode_fill_public_key(&node);
    SolKey key;
    std::copy_n(node.private_key, 32, key.private_key.begin());
    // trezor stores ed25519 public keys with a one-byte internal prefix.
    std::copy_n(node.public_key + 1, 32, key.public_key.begin());
    memzero(&node, sizeof node);
    return key;
}

std::string sol_address(std::span<const uint8_t, 32> pubkey)
{
    char buf[64];
    size_t size = sizeof buf;
    if (!b58enc(buf, &size, pubkey.data(), pubkey.size()))
        return {};
    return buf;
}

SolKey::~SolKey()
{
    memzero(private_key.data(), private_key.size());
}

}
