#include "core/crypto/hd.hpp"

#include <charconv>

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
    constexpr uint32_t kHardened = 0x80000000;

    // "44'" / "0h" / "1" → child index. nullopt on anything else.
    std::optional<uint32_t> parse_segment(std::string_view seg)
    {
        bool hardened = false;
        if (seg.ends_with('\'') || seg.ends_with('h') || seg.ends_with('H')) {
            hardened = true;
            seg.remove_suffix(1);
        }
        uint32_t index = 0;
        const auto [end, ec]
            = std::from_chars(seg.data(), seg.data() + seg.size(), index);
        if (ec != std::errc {} || end != seg.data() + seg.size()
            || index >= kHardened)
            return std::nullopt;
        return hardened ? index | kHardened : index;
    }

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
    bool first = true;
    while (!path.empty()) {
        const size_t slash = path.find('/');
        const std::string_view seg = path.substr(0, slash);
        path.remove_prefix(
            slash == std::string_view::npos ? path.size() : slash + 1);
        if (first && seg == "m") {
            first = false;
            continue;
        }
        first = false;
        const auto index = parse_segment(seg);
        if (!index)
            return std::nullopt;
        // The parent fingerprint rides along for serialization.
        key.parent_fingerprint_ = hdnode_fingerprint(&key.node_);
        if (hdnode_private_ckd(&key.node_, *index) == 0)
            return std::nullopt;
    }
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

HdKey::~HdKey()
{
    memzero(&node_, sizeof node_);
}

}
