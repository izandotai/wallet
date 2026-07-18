#pragma once

#include <cstddef>

namespace izan::ui {

// The amount, writ large: a centered, box-free numeric entry with an
// optional unit riding its baseline. The type shrinks step by step as
// the digits grow, so the full number is always visible — a sum that
// cannot be read in full cannot be reviewed. Returns true on Enter.
bool kit_amount_field(
    const char* id, char* buf, std::size_t size, const char* unit = nullptr);

}
