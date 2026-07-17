#include "keyd/signer.hpp"

#include <cstring>
#include <stdexcept>

#include <sodium.h>

extern "C" {
#include <sha3.h>
}

#include "core/crypto/bip39.hpp"
#include "core/crypto/eth.hpp"

namespace izan::keyd {

SignedDigest sign_payload(
    const secure::SecureBytes& entropy, std::span<const uint8_t> payload)
{
    if (entropy.empty())
        throw std::invalid_argument("signer: vault holds no seed");
    if (payload.empty())
        throw std::invalid_argument("signer: empty payload");

    SignedDigest out;
    keccak_256(payload.data(), payload.size(), out.digest.data());

    const secure::SecureBytes mnemonic = crypto::entropy_to_mnemonic(entropy);
    crypto::Seed seed = crypto::mnemonic_to_seed(
        reinterpret_cast<const char*>(mnemonic.data()), "");
    std::optional<crypto::HdKey> root = crypto::HdKey::from_seed(seed);
    sodium_memzero(seed.data(), seed.size());
    if (!root)
        throw std::runtime_error("signer: seed rejected");

    const std::optional<crypto::HdKey> key = root->derive(kEthAccountPath);
    if (!key)
        throw std::runtime_error("signer: account underivable");

    const std::optional<crypto::EcdsaSignature> sig
        = key->sign_digest(out.digest);
    if (!sig)
        throw std::runtime_error("signer: signing failed");
    out.sig = *sig;
    out.signer = crypto::eth_address(key->public_key_uncompressed());
    return out;
}

}
