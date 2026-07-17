#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace izan::crypto::detail {

inline constexpr uint32_t kHardened = 0x80000000;

// "44'" / "0h" / "1" → child index. nullopt on anything else.
inline std::optional<uint32_t> parse_segment(std::string_view seg)
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

// Walks "m/44'/0/1"-style paths (leading "m" optional), feeding each child
// index to step. Returns false on a malformed path or when step refuses.
template <typename Step>
bool walk_path(std::string_view path, Step&& step)
{
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
        if (!index || !step(*index))
            return false;
    }
    return true;
}

}
