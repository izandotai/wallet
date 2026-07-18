#pragma once

namespace izan::ui {

// One holding: symbol avatar, the asset over its chain, the balance
// flush right with its fiat worth whispering underneath. An unreadable
// chain shows its complaint instead of a number — never a silent zero.
// The row body is a button — the caller decides what touching an asset
// means (usually: send it). With a menu, a trailing three-dot slot
// opens the row's actions; a right-click anywhere on the row is the
// shortcut to the same place.
struct AssetRowEvent {
    bool clicked = false; // the row body — the caller's primary verb
    bool menu = false;    // the dots, or a right-click anywhere on the row
    bool hovered = false; // the row body, for the caller's tooltip
};

AssetRowEvent kit_asset_row(const char* id, const char* symbol,
    const char* chain, const char* balance, bool ok, const char* error_note,
    const char* fiat = nullptr, bool with_menu = false);

}
