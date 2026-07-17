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

namespace {

    crypto::HdKey account_key(const secure::SecureBytes& entropy)
    {
        if (entropy.empty())
            throw std::invalid_argument("signer: vault holds no seed");
        const secure::SecureBytes mnemonic
            = crypto::entropy_to_mnemonic(entropy);
        crypto::Seed seed = crypto::mnemonic_to_seed(
            reinterpret_cast<const char*>(mnemonic.data()), "");
        std::optional<crypto::HdKey> root = crypto::HdKey::from_seed(seed);
        sodium_memzero(seed.data(), seed.size());
        if (!root)
            throw std::runtime_error("signer: seed rejected");
        std::optional<crypto::HdKey> key = root->derive(kEthAccountPath);
        if (!key)
            throw std::runtime_error("signer: account underivable");
        return *key;
    }

}

SignedDigest sign_payload(
    const secure::SecureBytes& entropy, std::span<const uint8_t> payload)
{
    if (payload.empty())
        throw std::invalid_argument("signer: empty payload");

    SignedDigest out;
    keccak_256(payload.data(), payload.size(), out.digest.data());

    const crypto::HdKey key = account_key(entropy);
    const std::optional<crypto::EcdsaSignature> sig
        = key.sign_digest(out.digest);
    if (!sig)
        throw std::runtime_error("signer: signing failed");
    out.sig = *sig;
    out.signer = crypto::eth_address(key.public_key_uncompressed());
    return out;
}

std::string account_address(const secure::SecureBytes& entropy)
{
    return crypto::eth_address(account_key(entropy).public_key_uncompressed());
}

}
