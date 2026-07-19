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
#include <ed25519-donna/ed25519.h>
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

std::string sol_key_address(std::span<const uint8_t, 32> seed)
{
    uint8_t pub[32];
    ed25519_publickey(seed.data(), pub);
    const std::string addr = sol_address(std::span<const uint8_t, 32>(pub, 32));
    memzero(pub, sizeof pub);
    return addr;
}

std::optional<secure::SecureBytes> sol_key_from_base58(std::string_view text)
{
    // 64 bytes land at 86–88 base58 digits (fewer only with leading
    // zero bytes, which a real keypair does not have).
    if (text.size() < 80 || text.size() > 96)
        return std::nullopt;
    const std::string z(text); // decode wants a terminator
    uint8_t raw[64];
    size_t size = sizeof raw;
    const bool decoded = b58tobin(raw, &size, z.c_str()) && size == 64;
    std::optional<secure::SecureBytes> out;
    if (decoded) {
        uint8_t pub[32];
        ed25519_publickey(raw, pub);
        if (std::equal(pub, pub + 32, raw + 32)) {
            out.emplace(32);
            std::copy_n(raw, 32, out->data());
        }
        memzero(pub, sizeof pub);
    }
    memzero(raw, sizeof raw);
    return out;
}

secure::SecureBytes sol_key_to_base58(std::span<const uint8_t, 32> seed)
{
    uint8_t raw[64];
    std::copy_n(seed.data(), 32, raw);
    ed25519_publickey(raw, raw + 32);
    secure::SecureBytes out(96);
    size_t size = out.size();
    const bool ok
        = b58enc(reinterpret_cast<char*>(out.data()), &size, raw, sizeof raw);
    memzero(raw, sizeof raw);
    if (!ok)
        return {};
    return out;
}

SolKey::~SolKey()
{
    memzero(private_key.data(), private_key.size());
}

}

namespace izan::crypto {

bool valid_sol_address(std::string_view text)
{
    // 32 bytes land at 32-44 base58 digits.
    if (text.size() < 32 || text.size() > 44)
        return false;
    const std::string z(text);
    uint8_t raw[32];
    size_t size = sizeof raw;
    return b58tobin(raw, &size, z.c_str()) && size == 32;
}

}

namespace izan::crypto {

std::array<uint8_t, 64> sol_sign(
    std::span<const uint8_t, 32> seed, std::span<const uint8_t> message)
{
    std::array<uint8_t, 64> sig {};
    ed25519_sign(message.data(), message.size(), seed.data(), sig.data());
    return sig;
}

}
