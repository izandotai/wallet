#include "core/crypto/bip39.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include <bip39.h>
#include <memzero.h>
}

namespace izan::crypto {

Seed mnemonic_to_seed(std::string_view mnemonic, std::string_view passphrase)
{
    // trezor-crypto wants NUL-terminated strings. The temporaries carry key
    // material, so they are wiped before going back to the allocator.
    std::string m(mnemonic);
    std::string p(passphrase);
    Seed seed {};
    ::mnemonic_to_seed(m.c_str(), p.c_str(), seed.data(), nullptr);
    memzero(m.data(), m.size());
    memzero(p.data(), p.size());
    return seed;
}

bool mnemonic_valid(std::string_view mnemonic)
{
    std::string m(mnemonic);
    const bool ok = mnemonic_check(m.c_str()) != 0;
    memzero(m.data(), m.size());
    return ok;
}

secure::SecureBytes entropy_to_mnemonic(const secure::SecureBytes& entropy)
{
    if (entropy.size() != 16 && entropy.size() != 32)
        throw std::runtime_error("entropy must be 16 or 32 bytes");
    const char* m = mnemonic_from_data(entropy.data(), int(entropy.size()));
    if (m == nullptr)
        throw std::runtime_error("mnemonic generation failed");
    const std::size_t n = std::char_traits<char>::length(m);
    secure::SecureBytes out(n + 1); // keep the NUL: pages display as-is
    std::memcpy(out.data(), m, n + 1);
    mnemonic_clear(); // upstream keeps the sentence in a static buffer
    return out;
}

secure::SecureBytes mnemonic_to_entropy(std::string_view mnemonic)
{
    if (!mnemonic_valid(mnemonic))
        throw std::runtime_error("invalid mnemonic");
    std::string m(mnemonic);
    uint8_t bits[33] = {};
    const int total = mnemonic_to_bits(m.c_str(), bits);
    memzero(m.data(), m.size());
    if (total != 132 && total != 264) {
        memzero(bits, sizeof bits);
        throw std::runtime_error("unsupported mnemonic length");
    }
    // bits = entropy || checksum; ENT bytes = 4 per 33 total bits.
    secure::SecureBytes out(std::size_t(total / 33) * 4);
    std::memcpy(out.data(), bits, out.size());
    memzero(bits, sizeof bits);
    return out;
}

}
