#include "core/crypto/btc.hpp"

#include <array>
#include <cstring>

#include <sodium.h>

extern "C" {
// Upstream header trips C++20's volatile-parameter deprecation; not ours
// to fix, not ours to drown in either.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include <base58.h>
#include <memzero.h>
#include <bignum.h>
#include <ecdsa.h>
#include <hasher.h>
#include <ripemd160.h>
#include <secp256k1.h>
#include <segwit_addr.h>
#include <sha2.h>
#pragma GCC diagnostic pop
}

namespace izan::crypto {

namespace {

    std::array<uint8_t, 20> hash160(std::span<const uint8_t> bytes)
    {
        std::array<uint8_t, 32> sha;
        sha256_Raw(bytes.data(), bytes.size(), sha.data());
        std::array<uint8_t, 20> h160;
        ripemd160(sha.data(), sha.size(), h160.data());
        return h160;
    }

    std::string base58check_address(
        uint8_t version, const std::array<uint8_t, 20>& h160)
    {
        std::array<uint8_t, 21> payload;
        payload[0] = version;
        std::memcpy(payload.data() + 1, h160.data(), h160.size());
        char addr[40];
        if (base58_encode_check(payload.data(), int(payload.size()),
                HASHER_SHA2D, addr, sizeof addr)
            <= 0)
            return {};
        return addr;
    }

}

std::string btc_p2wpkh_address(
    std::span<const uint8_t, 33> pubkey, const char* hrp)
{
    // hash160 of the pubkey is the witness program.
    const std::array<uint8_t, 20> h160 = hash160(pubkey);
    char addr[93];
    if (segwit_addr_encode(addr, hrp, 0, h160.data(), h160.size()) != 1)
        return {};
    return addr;
}

std::string btc_p2pkh_address(std::span<const uint8_t, 33> pubkey)
{
    return base58check_address(0x00, hash160(pubkey));
}

std::string btc_p2sh_p2wpkh_address(std::span<const uint8_t, 33> pubkey)
{
    // The redeem script is the P2WPKH output: OP_0 PUSH20 <hash160(pub)>.
    std::array<uint8_t, 22> redeem { 0x00, 0x14 };
    const std::array<uint8_t, 20> h160 = hash160(pubkey);
    std::memcpy(redeem.data() + 2, h160.data(), h160.size());
    return base58check_address(0x05, hash160(redeem));
}

std::string btc_p2wsh_address(
    std::span<const uint8_t> witness_script, const char* hrp)
{
    std::array<uint8_t, 32> program;
    sha256_Raw(witness_script.data(), witness_script.size(), program.data());
    char addr[93];
    if (segwit_addr_encode(addr, hrp, 0, program.data(), program.size()) != 1)
        return {};
    return addr;
}

std::string btc_p2sh_p2wsh_address(std::span<const uint8_t> witness_script)
{
    // The redeem script is the P2WSH output: OP_0 PUSH32 <sha256(ws)>.
    std::array<uint8_t, 34> redeem { 0x00, 0x20 };
    sha256_Raw(witness_script.data(), witness_script.size(), redeem.data() + 2);
    return base58check_address(0x05, hash160(redeem));
}

std::string btc_p2tr_address(
    std::span<const uint8_t, 33> pubkey, const char* hrp)
{
    // BIP-341 key-path-only output: the internal key is the x-only form
    // of the pubkey (lifted to its even-y point), the tweak commits to
    // an empty script tree, and the output key is P + tweak·G.
    uint8_t even[33];
    even[0] = 0x02;
    std::memcpy(even + 1, pubkey.data() + 1, 32);
    curve_point p;
    if (!ecdsa_read_pubkey(&secp256k1, even, &p))
        return {};

    uint8_t tag[32];
    static constexpr char kTag[] = "TapTweak";
    sha256_Raw(reinterpret_cast<const uint8_t*>(kTag), sizeof kTag - 1, tag);
    SHA256_CTX ctx;
    sha256_Init(&ctx);
    sha256_Update(&ctx, tag, sizeof tag);
    sha256_Update(&ctx, tag, sizeof tag);
    sha256_Update(&ctx, even + 1, 32);
    uint8_t tweak[32];
    sha256_Final(&ctx, tweak);

    bignum256 t;
    bn_read_be(tweak, &t);
    if (!bn_is_less(&t, &secp256k1.order))
        return {}; // 2^-128 territory; refuse rather than reduce

    curve_point q;
    scalar_multiply(&secp256k1, &t, &q);
    point_add(&secp256k1, &p, &q);
    uint8_t out[32];
    bn_write_be(&q.x, out);
    char addr[93];
    if (segwit_addr_encode(addr, hrp, 1, out, sizeof out) != 1)
        return {};
    return addr;
}

std::optional<secure::SecureBytes> wif_to_key(std::string_view wif)
{
    // Base58Check payloads: 0x80 || key(32) [|| 0x01 compressed marker].
    if (wif.size() < 50 || wif.size() > 53)
        return std::nullopt;
    const std::string z(wif); // decode wants a terminator
    uint8_t decoded[40];
    const int n
        = base58_decode_check(z.c_str(), HASHER_SHA2D, decoded, sizeof decoded);
    std::optional<secure::SecureBytes> out;
    if ((n == 33 || n == 34) && decoded[0] == 0x80
        && (n == 33 || decoded[33] == 0x01)) {
        bool nonzero = false;
        for (int i = 1; i <= 32; ++i)
            nonzero = nonzero || decoded[i];
        if (nonzero) {
            out.emplace(32);
            std::memcpy(out->data(), decoded + 1, 32);
        }
    }
    sodium_memzero(decoded, sizeof decoded);
    return out;
}

}

namespace izan::crypto {

bool valid_btc_address(std::string_view text)
{
    if (text.size() < 26 || text.size() > 90)
        return false;
    const std::string z(text);
    if (text.starts_with("bc1") || text.starts_with("BC1")) {
        int version = 0;
        uint8_t program[40];
        size_t program_len = 0;
        return segwit_addr_decode(
                   &version, program, &program_len, "bc", z.c_str())
            == 1;
    }
    if (text[0] == '1' || text[0] == '3') {
        uint8_t payload[21];
        return base58_decode_check(
                   z.c_str(), HASHER_SHA2D, payload, sizeof payload)
            == sizeof payload
            && (payload[0] == 0x00 || payload[0] == 0x05);
    }
    return false;
}

}

namespace izan::crypto {

namespace {

    void tagged_sha256(
        const char* tag, std::span<const uint8_t> data, uint8_t out[32])
    {
        uint8_t tag_hash[32];
        sha256_Raw(
            reinterpret_cast<const uint8_t*>(tag), strlen(tag), tag_hash);
        SHA256_CTX ctx;
        sha256_Init(&ctx);
        sha256_Update(&ctx, tag_hash, 32);
        sha256_Update(&ctx, tag_hash, 32);
        sha256_Update(&ctx, data.data(), data.size());
        sha256_Final(&ctx, out);
    }

    // The scalar behind an even-Y public key: BIP-340 keys are x-only,
    // so a key whose point looks down gets negated to look up.
    void even_y_scalar(const bignum256& d0, bignum256& d, uint8_t px[32])
    {
        curve_point p;
        if (scalar_multiply(&secp256k1, &d0, &p) != 0)
            throw std::runtime_error("bip340: scalar_multiply failed");
        d = d0;
        if (bn_is_odd(&p.y))
            bn_subtract(&secp256k1.order, &d0, &d);
        bn_write_be(&p.x, px);
    }

}

std::array<uint8_t, 64> bip340_sign(std::span<const uint8_t, 32> seckey,
    std::span<const uint8_t, 32> msg, std::span<const uint8_t, 32> aux)
{
    bignum256 d0;
    bn_read_be(seckey.data(), &d0);
    if (bn_is_zero(&d0) || !bn_is_less(&d0, &secp256k1.order))
        throw std::invalid_argument("bip340: key out of range");
    bignum256 d;
    uint8_t px[32];
    even_y_scalar(d0, d, px);

    // t = d XOR H_aux(aux); nonce = H_nonce(t || P.x || m)
    uint8_t dbytes[32];
    bn_write_be(&d, dbytes);
    uint8_t th[32];
    tagged_sha256("BIP0340/aux", aux, th);
    for (int i = 0; i < 32; ++i)
        th[i] ^= dbytes[i];
    uint8_t nonce_msg[96];
    memcpy(nonce_msg, th, 32);
    memcpy(nonce_msg + 32, px, 32);
    memcpy(nonce_msg + 64, msg.data(), 32);
    uint8_t rand[32];
    tagged_sha256(
        "BIP0340/nonce", std::span<const uint8_t>(nonce_msg, 96), rand);
    bignum256 k0;
    bn_read_be(rand, &k0);
    bn_mod(&k0, &secp256k1.order);
    if (bn_is_zero(&k0))
        throw std::runtime_error("bip340: zero nonce");

    bignum256 k;
    uint8_t rx[32];
    even_y_scalar(k0, k, rx);

    uint8_t chal[96];
    memcpy(chal, rx, 32);
    memcpy(chal + 32, px, 32);
    memcpy(chal + 64, msg.data(), 32);
    uint8_t eh[32];
    tagged_sha256("BIP0340/challenge", std::span<const uint8_t>(chal, 96), eh);
    bignum256 e;
    bn_read_be(eh, &e);
    bn_mod(&e, &secp256k1.order);

    // s = (k + e·d) mod n
    bn_multiply(&d, &e, &secp256k1.order);
    bn_addmod(&e, &k, &secp256k1.order);
    bn_mod(&e, &secp256k1.order);

    std::array<uint8_t, 64> sig {};
    memcpy(sig.data(), rx, 32);
    bn_write_be(&e, sig.data() + 32);
    memzero(dbytes, sizeof dbytes);
    memzero(&d, sizeof d);
    memzero(&k, sizeof k);
    memzero(&k0, sizeof k0);
    return sig;
}

std::array<uint8_t, 32> bip341_tweak_seckey(std::span<const uint8_t, 32> sk)
{
    bignum256 d0;
    bn_read_be(sk.data(), &d0);
    if (bn_is_zero(&d0) || !bn_is_less(&d0, &secp256k1.order))
        throw std::invalid_argument("bip341: key out of range");
    bignum256 d;
    uint8_t px[32];
    even_y_scalar(d0, d, px);
    uint8_t th[32];
    tagged_sha256("TapTweak", std::span<const uint8_t>(px, 32), th);
    bignum256 t;
    bn_read_be(th, &t);
    bn_mod(&t, &secp256k1.order);
    bn_addmod(&d, &t, &secp256k1.order);
    bn_mod(&d, &secp256k1.order);
    std::array<uint8_t, 32> out {};
    bn_write_be(&d, out.data());
    memzero(&d, sizeof d);
    return out;
}

}
