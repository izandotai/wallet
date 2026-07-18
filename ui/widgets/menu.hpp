#pragma once

namespace izan::ui {

// Popup menus in the menu bar's voice — the same padding, row rhythm
// and opaque backdrop as the shell's dropdowns, so a context menu, a
// chain picker and a main menu read as one family. Navigation focus
// rings stay off: menus are pointer terrain.
//
//   if (kit_menu_begin("##menu")) {
//       if (kit_menu_item(label, trailing, selected, enabled)) { ... }
//       kit_menu_end();
//   }
bool kit_menu_begin(const char* id);
void kit_menu_end();
bool kit_menu_item(const char* label, const char* trailing = nullptr,
    bool selected = false, bool enabled = true);

// A menu row led by a color swatch minted from a name — for pickers
// whose entries are things (assets, wallets) rather than verbs. Every
// row in one menu must share one width or the highlights come out
// ragged: measure the widest row first, then pass it to each. The
// caller wraps rows in PushID when labels repeat.
float kit_menu_row_width(const char* label, const char* trailing = nullptr);
bool kit_menu_item_icon(const char* swatch_name, const char* label,
    const char* trailing = nullptr, bool selected = false, float width = 0.0f);

}
