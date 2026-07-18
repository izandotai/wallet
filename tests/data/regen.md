# chain-family address vectors (family_test.cpp) provenance

The BIP-44/49 mainnet and Solana (SLIP-0010, m/44'/501'/i'/0') addresses
asserted in family_test.cpp come from `derive_vectors.py` in this
directory: an independent pure-stdlib implementation of BIP-39, BIP-32,
SLIP-0010 ed25519 and every address codec, which self-validates against
the official BIP-84 and BIP-86 spec vectors before emitting anything.
The BIP-86 addresses are the spec's own; the key-wallet addresses are
the same script run over the Bitcoin wiki's canonical example key.
To regenerate: `python derive_vectors.py`.

# bip39_vectors.inc provenance

Vendored from the canonical BIP-39 test vector set:
https://raw.githubusercontent.com/trezor/python-mnemonic/master/vectors.json
(English list, passphrase "TREZOR", 24 vectors).

To regenerate: fetch vectors.json and emit each `[entropy, mnemonic, seed]`
triple as a `Bip39Vector` initializer. Never edit the .inc by hand.
