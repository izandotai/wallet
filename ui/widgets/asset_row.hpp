#pragma once

namespace izan::ui {

// One holding: symbol avatar, the asset over its chain, the balance
// flush right with its fiat worth whispering underneath. An unreadable
// chain shows its complaint instead of a number — never a silent zero.
// The whole row is a button: hovering washes it, clicking returns
// true — the caller decides what touching an asset means (usually:
// send it).
bool kit_asset_row(const char* id, const char* symbol, const char* chain,
    const char* balance, bool ok, const char* error_note,
    const char* fiat = nullptr);

}
