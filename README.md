# Izan Wallet — core

Native desktop wallet core: vault, HD derivation, signing authority, and
transaction engine. Single static binary, no Electron, no browser.

- **Keys never leave the signer process.** The UI, scripts, and AI agents can
  only *propose* transactions; signing happens in an isolated process behind a
  human-controlled approval queue.
- **Zero swap fees.** The wallet is free for humans.
- **Reproducible builds.** Every release ships with build instructions that let
  you produce a byte-identical binary.
- **Tested against the specs.** Every cryptographic component lands together
  with the official test vectors of the standard it implements, vendored
  verbatim from the spec (see `tests/data/`).

## Status

Early development — nothing here is ready for real funds.

- [x] BIP-39 mnemonic → seed (24 official vectors)
- [x] BIP-32 HD derivation, xprv/xpub (all 17 spec chains)
- [x] Ethereum addresses, EIP-55 checksums (spec test cases, end-to-end
      against the ecosystem's best-known mnemonic→address pair)
- [x] Bitcoin P2WPKH addresses (BIP-84 official vectors)
- [x] Solana keys, SLIP-0010 ed25519 (both spec vectors, hardened-only)
- [x] Encrypted vault (argon2id, locked memory, wipe-on-drop)
- [x] Exact integer amounts (U256, decimal ↔ base-unit conversion)
- [x] keyd process split: vault custody in a spawned, hardened child
      (anonymous-pipe password channel under a one-shot session key,
      crash dumps off, dynamic code and non-Microsoft DLLs blocked,
      wipe-and-exit on parent death)
- [ ] Proposal/approval protocol, policy engine, signing
- [x] Chain registry (config-driven EVM list) and JSON-RPC codec
- [x] HTTPS transport (Boost.Beast, static OpenSSL, OS root-store trust,
      live-tested against mainnet RPC)
- [x] Balance reads: native + ERC-20 (minimal ABI codec, endpoint
      failover, live-tested on Ethereum and Robinhood Chain)
- [x] Watch-only portfolio: token list config (decimals audited against
      the chain), multi-chain snapshot, `izan_watch` CLI
- [ ] Transaction engine (EIP-1559 first)
- [ ] UI shell

## Build

Requires CMake ≥ 3.28, Ninja, and a C++23 toolchain. All third-party
dependencies are pinned and built from source via FetchContent.

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Binaries are fully static; the test executable's import table is audited to
contain nothing beyond Windows system libraries.

Distribution builds strip symbols, drop unreferenced sections and scrub
the builder's source paths out of the binaries:

```
cmake -S . -B build-dist -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-dist
```

Byte-identical reproduction additionally requires the canonical build
path stated in the release notes: OpenSSL embeds its install prefix as
an inert string (never dereferenced — config autoloading is compiled
out and a static build loads no engines or providers).

## License

GPL-3.0-only. Copyright (c) 2026 izan.ai.

Izan is free software: you can use, study, share and improve it under
the terms of the GNU General Public License version 3 (see LICENSE).
Distributing modified versions requires publishing your changes under
the same license.

Commercial licensing (for embedding Izan in proprietary products) is
available separately — contact izan.ai. To keep dual licensing
possible, external contributions are accepted only with a copyright
assignment / CLA.
