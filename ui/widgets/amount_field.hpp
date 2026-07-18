#pragma once

#include <cstddef>

namespace izan::ui {

// The amount row: a framed field wearing the kit's field dress, digits
// in larger type on the left, and an embedded badge button on the
// right edge (the network or unit — the caller decides what it means
// and what pressing it does). Width comes from SetNextItemWidth.
// Returns true on Enter; *badge_clicked reports a press on the badge.
bool kit_amount_field(const char* id, char* buf, std::size_t size,
    const char* badge, bool* badge_clicked = nullptr);

}
