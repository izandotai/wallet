cmake_minimum_required(VERSION 3.28)
project(izan-wallet-core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fully static binaries: the only DLLs an izan executable may import are
# Windows system libraries. Audited in CI on every release.
# Optimization is load-bearing on mingw: at -O0 any TU that compares
# std::type_info (std::function internals do) emits an out-of-line copy
# of the C++23 inline operator==, which collides with libstdc++'s
# tinfo.o at static link (GCC 14.2 toolchain bug). At -O2 the call
# inlines away and the collision never forms.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(MINGW)
    add_link_options(-static)
    # Windows 10 API surface (process mitigation policies live there).
    add_compile_definitions(_WIN32_WINNT=0x0A00)
    # DEP/ASLR link bits set explicitly — mingw does not guarantee the
    # defaults — and audited from inside the binary by linkbits_test,
    # which also checks the relocations that make the ASLR bit real.
    # CFG is not attainable through GCC; recorded, not claimed.
    add_link_options(-Wl,--nxcompat -Wl,--dynamicbase -Wl,--high-entropy-va)
endif()

# Distribution builds (-DCMAKE_BUILD_TYPE=Release): strip symbols, drop
# unreferenced sections and scrub local source paths. A shipped binary
# must carry no home-directory strings and no debug info — both leak
# the builder's machine and break byte-identical reproduction. Everyday
# builds stay RelWithDebInfo.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-ffunction-sections -fdata-sections
        "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
    add_link_options(-Wl,--gc-sections)
    if(MINGW)
        add_link_options(-s)
    endif()
endif()

include(FetchContent)
include(cmake/openssl.cmake)

# Boost beast/asio for HTTPS and websockets, from the official CMake
# release tarball.
set(BOOST_INCLUDE_LIBRARIES beast asio)
set(BOOST_ENABLE_CMAKE ON)
set(BOOST_SKIP_INSTALL_RULES ON)
FetchContent_Declare(Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
)

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
# Reflection-based JSON for configs and RPC envelopes. A wallet has no
# hot parse path; one JSON library is enough.
FetchContent_Declare(glaze
    URL https://github.com/stephenberry/glaze/archive/refs/tags/v7.8.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(trezor_firmware doctest sodium glaze Boost)

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
    core/crypto/secret_import.cpp
    core/crypto/sol.cpp
)
target_include_directories(izan_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_crypto PUBLIC izan_trezor izan_secure)

# ABI words and selectors for contract reads (keccak from trezor).
add_library(izan_codec STATIC
    core/codec/abi.cpp
    core/codec/rlp.cpp
)
target_include_directories(izan_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_codec PUBLIC izan_units PRIVATE izan_trezor)

# L1 platform: process separation primitives — anonymous pipes, the
# secretstream password channel, whitelist-inherited child spawn.
# Windows-first; the Debian port arrives with its production target.
if(WIN32)
    add_library(izan_ipc STATIC
        platform/ipc/named_pipe.cpp
        platform/ipc/pipe.cpp
        platform/ipc/secret_channel.cpp
        platform/proc/child_process.cpp
    )
    target_include_directories(izan_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(izan_ipc PUBLIC izan_secure)

    # Trust plane: vault custody behind the password channel. Links
    # primitives only — no chains, no net, no UI, by construction.
    add_library(izan_keyd STATIC
        keyd/audit.cpp
        keyd/autolock.cpp
        keyd/child.cpp
        keyd/client.cpp
        keyd/hardening.cpp
        keyd/signer.cpp
    )
    target_include_directories(izan_keyd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(izan_keyd PUBLIC izan_ipc izan_secure
        PRIVATE izan_crypto izan_soltx)
endif()

# L1 platform: HTTPS transport and the user's clock. TLS trust anchors
# come from the OS root store — the static OpenSSL has no default
# certificate paths — and the timezone comes from the OS as well.
add_library(izan_platform STATIC
    platform/net/http_client.cpp
    platform/net/tls.cpp
    platform/time/local_clock.cpp
)
target_include_directories(izan_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_platform
    PUBLIC Boost::beast Boost::asio izan_openssl)
if(WIN32)
    target_link_libraries(izan_platform PUBLIC ws2_32 mswsock)
endif()

# Offline transaction grammar shared by the trust plane and the
# domain: the encoder the send flow speaks and the whitelist parser
# the approval screen reads. No network, ever.
add_library(izan_soltx STATIC
    domain/sol/sol_tx.cpp
)
target_include_directories(izan_soltx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_soltx PRIVATE izan_crypto)

# L3 domain: chain registry and RPC plumbing. Links primitives only —
# never the trust plane.
add_library(izan_domain STATIC
    domain/chains/chain_spec.cpp
    domain/chains/jsonrpc.cpp
    domain/chains/rpc_client.cpp
    domain/config/config_trust.cpp
    domain/assets/balances.cpp
    domain/assets/history.cpp
    domain/assets/portfolio.cpp
    domain/assets/prices.cpp
    domain/assets/token_registry.cpp
    domain/tx/eip1559.cpp
    domain/tx/txflow.cpp
    domain/swap/lifi.cpp
    domain/btc/esplora.cpp
    domain/sol/solana.cpp
)
target_include_directories(izan_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_domain PUBLIC izan_units izan_soltx
    PRIVATE glaze::glaze izan_codec izan_crypto izan_platform)

# UI string catalogs. Headless on purpose: pages read it, tests audit
# it, neither needs a window.
add_library(izan_i18n STATIC
    ui/i18n/catalog.cpp
)
target_include_directories(izan_i18n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(izan_i18n PRIVATE glaze::glaze)

# The wallet UI's headless layer: directory store, import model, keyd
# session, preset naming. Everything a page decides lives here where
# tests can reach it; the page files only draw.
if(WIN32)
    add_library(izan_wallet_ui STATIC
        ui/wallet/import_model.cpp
        ui/wallet/presets.cpp
        ui/wallet/session.cpp
        ui/wallet/store.cpp
    )
    target_include_directories(izan_wallet_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(izan_wallet_ui PUBLIC izan_keyd izan_crypto
        izan_secure PRIVATE glaze::glaze)
endif()

# Watch-only CLI: paste an address, see the assets.
add_executable(izan_watch apps/watch_main.cpp)
target_link_libraries(izan_watch PRIVATE izan_domain izan_units)

# L5 shell. Heavy fetch (GLFW/FreeType/ImGui); switch it off to keep a
# trust-plane-only build fast.
option(IZAN_BUILD_UI "Build the wallet UI" ON)
if(IZAN_BUILD_UI AND WIN32)
    include(cmake/gui_deps.cmake)

    add_library(izan_ui_shell STATIC
        ui/shell/app.cpp
        ui/shell/chrome_widgets.cpp
        ui/shell/fonts.cpp
        ui/shell/ime.cpp
        ui/shell/theme.cpp
        ui/shell/ui_layout.cpp
        ui/shell/win_chrome.cpp
        ui/shell/win_console.cpp
        ui/shell/win_cursors.cpp
    )
    target_include_directories(izan_ui_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(izan_ui_shell PUBLIC izan_imgui dwmapi imm32)

    # Runtime assets (fonts; cursor files are optional and unshipped in
    # the open repo) copied beside the target exe.
    function(izan_ui_copy_assets target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/ui/assets
                $<TARGET_FILE_DIR:${target}>/assets
            COMMENT "Copying ui assets for ${target}")
    endfunction()

    # QR codes for receive flows: nayuki's reference implementation,
    # pinned tarball, a single translation unit compiled into the kit.
    FetchContent_Declare(qrcodegen
        URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SOURCE_SUBDIR cmake-subdir-does-not-exist
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(qrcodegen)

    # The kit: izan's own widget library — every component drawn in the
    # active design language. Its own library so the smoke harness can
    # host a component gallery without dragging the wallet in.
    add_library(izan_kit STATIC
        ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp
        ui/widgets/address_field.cpp
        ui/widgets/amount_field.cpp
        ui/widgets/asset_row.cpp
        ui/widgets/avatar.cpp
        ui/widgets/button.cpp
        ui/widgets/card.cpp
        ui/widgets/choice_row.cpp
        ui/widgets/copy_text.cpp
        ui/widgets/design.cpp
        ui/widgets/dialog.cpp
        ui/widgets/empty_state.cpp
        ui/widgets/hyperlink.cpp
        ui/widgets/identity.cpp
        ui/widgets/label.cpp
        ui/widgets/list_row.cpp
        ui/widgets/menu.cpp
        ui/widgets/pill.cpp
        ui/widgets/qr.cpp
        ui/widgets/result_mark.cpp
        ui/widgets/search_field.cpp
        ui/widgets/select.cpp
        ui/widgets/spinner.cpp
        ui/widgets/step_dots.cpp
        ui/widgets/table.cpp
        ui/widgets/text_field.cpp
        ui/widgets/toggle.cpp
        ui/widgets/tooltip.cpp
        ui/widgets/tx_row.cpp
    )
    target_include_directories(izan_kit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE ${qrcodegen_SOURCE_DIR}/cpp)
    # shell32: kit_hyperlink hands URLs to the system browser.
    target_link_libraries(izan_kit PUBLIC izan_imgui izan_secure shell32)

    # WIN32 = windows subsystem: no console flashes up on double-click;
    # launched from a terminal, try_attach_parent_console re-plumbs
    # stdout/stderr back to it.
    add_executable(izan_ui_smoke WIN32 ui/smoke_main.cpp)
    target_link_libraries(
        izan_ui_smoke PRIVATE izan_ui_shell izan_kit glaze::glaze)
    izan_ui_copy_assets(izan_ui_smoke)

    # The wallet. One binary, two personalities (UI / --keyd-child).
    add_executable(izan WIN32
        apps/izan_main.cpp
        ui/pages/portfolio_page.cpp
        ui/pages/history_page.cpp
        ui/pages/send_page.cpp
        ui/pages/swap_page.cpp
        ui/pages/vault_page.cpp
        ui/pages/vault/accounts_view.cpp
        ui/pages/vault/create_view.cpp
        ui/pages/vault/import_view.cpp
        ui/pages/vault/list_view.cpp
        ui/pages/vault/secret_view.cpp
        ui/pages/vault/unlock_view.cpp
    )
    target_link_libraries(izan PRIVATE izan_ui_shell izan_kit izan_i18n
        izan_keyd izan_wallet_ui izan_crypto izan_secure izan_domain
        izan_units glaze::glaze)
    izan_ui_copy_assets(izan)
    add_custom_command(TARGET izan POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/data
            $<TARGET_FILE_DIR:izan>/data
        COMMENT "Copying chain/token config for izan")
endif()

enable_testing()
add_executable(izan_tests
    tests/main.cpp
    tests/bip39_test.cpp
    tests/btc_test.cpp
    tests/eth_test.cpp
    tests/hd_test.cpp
    tests/sol_test.cpp
    tests/secure_bytes_test.cpp
    tests/vault_test.cpp
    tests/units_test.cpp
    tests/chains_test.cpp
    tests/net_test.cpp
    tests/abi_test.cpp
    tests/assets_test.cpp
    tests/portfolio_test.cpp
    tests/prices_test.cpp
    tests/history_test.cpp
    tests/config_trust_test.cpp
    tests/i18n_test.cpp
    tests/tx_test.cpp
    tests/txflow_test.cpp
    tests/family_test.cpp
    tests/import_test.cpp
    tests/wallet_ui_test.cpp
    tests/keyd_test.cpp
    tests/linkbits_test.cpp
    tests/proposals_test.cpp
    tests/local_clock_test.cpp
    tests/swap_test.cpp
    tests/btc_domain_test.cpp
    tests/sol_domain_test.cpp
)
target_link_libraries(izan_tests PRIVATE izan_crypto izan_secure izan_units
    izan_codec izan_domain izan_i18n izan_platform doctest::doctest)
if(WIN32)
    target_link_libraries(izan_tests PRIVATE izan_keyd izan_wallet_ui)
endif()
target_compile_definitions(izan_tests
    PRIVATE IZAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(izan_tests PRIVATE tests)
add_test(NAME izan_tests COMMAND izan_tests)
