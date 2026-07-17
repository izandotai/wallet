#include "core/crypto/bip39.hpp"

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

}
