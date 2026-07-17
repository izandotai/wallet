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
- [ ] Signer process (keyd) with proposal/approval protocol
- [x] Chain registry (config-driven EVM list) and JSON-RPC codec
- [ ] Chain RPC transport, balances, watch-only mode
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

## License

MIT
