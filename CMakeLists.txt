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
# libsodium behind a CMake wrapper, both pinned (wrapper commit → bundled
# libsodium submodule). Guarded allocations for key material live here.
set(SODIUM_DISABLE_TESTS ON)
FetchContent_Declare(sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG 9b2848dfc1b917a9410f0de9d81059b26cbfaa8d
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(trezor_firmware doctest sodium)

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
# trezor's blake2b and ed25519-donna ge25519_* collide with libsodium's
# internal symbols once both archives meet in one binary; rename at
# compile time.
target_compile_definitions(izan_trezor PRIVATE
    blake2b=izan_trezor_blake2b blake2b_Key=izan_trezor_blake2b_Key
    blake2b_init_param=izan_trezor_blake2b_init_param
    ge25519_add=izan_trezor_ge25519_add
    ge25519_scalarmult=izan_trezor_ge25519_scalarmult
    ge25519_double_scalarmult_vartime=izan_trezor_ge25519_dsm_vartime)
target_compile_options(izan_trezor PRIVATE -w)
if(WIN32)
    target_link_libraries(izan_trezor PUBLIC bcrypt)
endif()

# Exact integer money: U256 amounts and decimal conversion, header-only.
add_library(izan_units INTERFACE)
target_include_directories(izan_units INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Key-material hygiene: guarded SecureBytes container and the encrypted
# vault file, both over libsodium.
add_library(izan_secure STATIC
    core/secure/vault.cpp
)
target_include_directories(izan_secure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_secure PUBLIC sodium)

add_library(izan_crypto STATIC
    core/crypto/bip39.cpp
    core/crypto/btc.cpp
    core/crypto/eth.cpp
    core/crypto/hd.cpp
    core/crypto/sol.cpp
)
target_include_directories(izan_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_crypto PUBLIC izan_trezor)

enable_testing()
add_executable(izan_tests
    tests/bip39_test.cpp
    tests/btc_test.cpp
    tests/eth_test.cpp
    tests/hd_test.cpp
    tests/sol_test.cpp
    tests/secure_bytes_test.cpp
    tests/vault_test.cpp
    tests/units_test.cpp
)
target_link_libraries(izan_tests PRIVATE izan_crypto izan_secure izan_units
    doctest::doctest)
target_include_directories(izan_tests PRIVATE tests)
add_test(NAME izan_tests COMMAND izan_tests)
