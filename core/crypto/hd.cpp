#include "core/crypto/hd.hpp"

#include "core/crypto/detail/path.hpp"

extern "C" {
#include <curves.h>
#include <ecdsa.h>
#include <memzero.h>
#include <secp256k1.h>
}

namespace izan::crypto {

namespace {

    constexpr uint32_t kVersionPrivate = 0x0488ade4; // xprv
    constexpr uint32_t kVersionPublic = 0x0488b21e;  // xpub

}

std::optional<HdKey> HdKey::from_seed(std::span<const uint8_t> seed)
{
    HdKey key;
    if (hdnode_from_seed(seed.data(), static_cast<int>(seed.size()),
            SECP256K1_NAME, &key.node_)
        == 0)
        return std::nullopt;
    return key;
}

std::optional<HdKey> HdKey::derive(std::string_view path) const
{
    HdKey key = *this;
    const bool ok = detail::walk_path(path, [&](uint32_t index) {
        // The parent fingerprint rides along for serialization.
        key.parent_fingerprint_ = hdnode_fingerprint(&key.node_);
        return hdnode_private_ckd(&key.node_, index) != 0;
    });
    if (!ok)
        return std::nullopt;
    return key;
}

std::string HdKey::xprv() const
{
    char buf[120];
    hdnode_serialize_private(
        &node_, parent_fingerprint_, kVersionPrivate, buf, sizeof buf);
    std::string out(buf);
    memzero(buf, sizeof buf);
    return out;
}

std::string HdKey::xpub() const
{
    HDNode node = node_;
    hdnode_fill_public_key(&node);
    char buf[120];
    hdnode_serialize_public(
        &node, parent_fingerprint_, kVersionPublic, buf, sizeof buf);
    std::string out(buf);
    memzero(&node, sizeof node);
    memzero(buf, sizeof buf);
    return out;
}

std::array<uint8_t, 65> HdKey::public_key_uncompressed() const
{
    std::array<uint8_t, 65> out {};
    ecdsa_get_public_key65(&secp256k1, node_.private_key, out.data());
    return out;
}

std::array<uint8_t, 33> HdKey::public_key_compressed() const
{
    std::array<uint8_t, 33> out {};
    ecdsa_get_public_key33(&secp256k1, node_.private_key, out.data());
    return out;
}

HdKey::~HdKey()
{
    memzero(&node_, sizeof node_);
}

}
