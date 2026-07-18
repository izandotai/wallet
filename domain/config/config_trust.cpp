#include "domain/config/config_trust.hpp"

#include <cstdint>

extern "C" {
#include <sha2.h>
}

namespace izan::config {

namespace {

    struct Shipped {
        std::string_view name;
        std::string_view sha256;
    };

    // Updated whenever a default config changes; config_trust_test
    // recomputes these from data/ and fails the build on drift, so the
    // table cannot silently rot.
    constexpr Shipped kShipped[] = {
        { "chains.json",
            "0cf8a6bb4239677fcf41ede0d9f41e7c00a7ca568874779d139cb518cecca82"
            "a" },
        { "tokens.json",
            "21f58e7e7970823239a9a201d3f999b11cf8da9b4d15a87e567066e56b3b63"
            "9c" },
    };

}

std::string sha256_hex(std::string_view data)
{
    uint8_t digest[SHA256_DIGEST_LENGTH];
    sha256_Raw(
        reinterpret_cast<const uint8_t*>(data.data()), data.size(), digest);
    std::string out;
    out.reserve(sizeof digest * 2);
    constexpr char hex[] = "0123456789abcdef";
    for (const uint8_t b : digest) {
        out += hex[b >> 4];
        out += hex[b & 0xf];
    }
    return out;
}

Trust classify(std::string_view filename, std::string_view contents)
{
    for (const Shipped& s : kShipped)
        if (s.name == filename)
            return sha256_hex(contents) == s.sha256 ? Trust::ShippedDefault
                                                    : Trust::Modified;
    return Trust::Modified;
}

}
