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

}
