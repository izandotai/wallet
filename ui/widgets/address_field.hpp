#pragma once

#include <cstddef>

namespace izan::ui {

// The address field: a text entry that knows it holds a crypto
// address. Empty, it wears a one-click paste glyph; filled, a
// one-click clear; right-click serves paste / copy / clear. Labels
// arrive from the caller — the kit speaks no language of its own.
// Returns true on Enter.
bool kit_address_field(const char* id, const char* hint, char* buf,
    std::size_t size, const char* paste_label, const char* copy_label,
    const char* clear_label);

}
