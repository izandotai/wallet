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
