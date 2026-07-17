cmake_minimum_required(VERSION 3.28)
project(izan-wallet-core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fully static binaries: the only DLLs an izan executable may import are
# Windows system libraries. Audited in CI on every release.
if(MINGW)
    add_link_options(-static)
endif()

include(FetchContent)

# trezor-crypto lives inside the trezor-firmware monorepo; pin a release
# tarball and build only the modules we use.
FetchContent_Declare(trezor_firmware
    URL https://github.com/trezor/trezor-firmware/archive/refs/tags/core/v2.8.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR cmake-subdir-does-not-exist # sources only, no add_subdirectory
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(trezor_firmware doctest)

set(TREZOR ${trezor_firmware_SOURCE_DIR}/crypto)

# BIP-39 needs only the hash/KDF subset. Randomness comes from the OS CSPRNG
# (platform/rand.c); upstream rand.c is a weak placeholder and is never built.
add_library(izan_trezor STATIC
    ${TREZOR}/bip39.c
    ${TREZOR}/bip39_english.c
    ${TREZOR}/hmac.c
    ${TREZOR}/memzero.c
    ${TREZOR}/pbkdf2.c
    ${TREZOR}/sha2.c
    # BIP-32 and its hard dependencies (bip32.c pulls in ed25519/aes
    # unconditionally; ed25519 doubles for Solana later)
    ${TREZOR}/address.c
    ${TREZOR}/base58.c
    ${TREZOR}/bignum.c
    ${TREZOR}/bip32.c
    ${TREZOR}/blake256.c
    ${TREZOR}/blake2b.c
    ${TREZOR}/blake2s.c
    ${TREZOR}/curves.c
    ${TREZOR}/ecdsa.c
    ${TREZOR}/groestl.c
    ${TREZOR}/hasher.c
    ${TREZOR}/hmac_drbg.c
    ${TREZOR}/nist256p1.c
    ${TREZOR}/rfc6979.c
    ${TREZOR}/ripemd160.c
    ${TREZOR}/secp256k1.c
    ${TREZOR}/sha3.c
    ${TREZOR}/segwit_addr.c
    ${TREZOR}/ed25519-donna/curve25519-donna-32bit.c
    ${TREZOR}/ed25519-donna/curve25519-donna-helpers.c
    ${TREZOR}/ed25519-donna/curve25519-donna-scalarmult-base.c
    ${TREZOR}/ed25519-donna/ed25519.c
    ${TREZOR}/ed25519-donna/ed25519-donna-32bit-tables.c
    ${TREZOR}/ed25519-donna/ed25519-donna-basepoint-table.c
    ${TREZOR}/ed25519-donna/ed25519-donna-impl-base.c
    ${TREZOR}/ed25519-donna/ed25519-keccak.c
    ${TREZOR}/ed25519-donna/ed25519-sha3.c
    ${TREZOR}/ed25519-donna/modm-donna-32bit.c
    ${TREZOR}/aes/aes_modes.c
    ${TREZOR}/aes/aescrypt.c
    ${TREZOR}/aes/aeskey.c
    ${TREZOR}/aes/aestab.c
    platform/rand.c
)
target_include_directories(izan_trezor PUBLIC ${TREZOR})
# Upstream caches the mnemonic→seed mapping in static memory; that violates
# the rule that all key material must be wipeable, so the caches stay off.
target_compile_definitions(izan_trezor PUBLIC USE_KECCAK=1
    USE_BIP39_CACHE=0 USE_BIP32_CACHE=0)
target_compile_options(izan_trezor PRIVATE -w)
if(WIN32)
    target_link_libraries(izan_trezor PUBLIC bcrypt)
endif()

add_library(izan_crypto STATIC
    core/crypto/bip39.cpp
    core/crypto/eth.cpp
    core/crypto/hd.cpp
)
target_include_directories(izan_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_crypto PUBLIC izan_trezor)

enable_testing()
add_executable(izan_tests
    tests/bip39_test.cpp
    tests/eth_test.cpp
    tests/hd_test.cpp
)
target_link_libraries(izan_tests PRIVATE izan_crypto doctest::doctest)
target_include_directories(izan_tests PRIVATE tests)
add_test(NAME izan_tests COMMAND izan_tests)
