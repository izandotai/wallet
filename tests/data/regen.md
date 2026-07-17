# bip39_vectors.inc provenance

Vendored from the canonical BIP-39 test vector set:
https://raw.githubusercontent.com/trezor/python-mnemonic/master/vectors.json
(English list, passphrase "TREZOR", 24 vectors).

To regenerate: fetch vectors.json and emit each `[entropy, mnemonic, seed]`
triple as a `Bip39Vector` initializer. Never edit the .inc by hand.
