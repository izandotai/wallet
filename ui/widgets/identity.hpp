#pragma once

namespace izan::ui {

// The identity header: avatar, name and a supporting line stacked on
// the center axis, with an optional hero line beneath — a net worth,
// a state. The composition the lock screen pioneered, packaged for
// every page that opens with "who, then the one number that matters".
// When copy labels are given, the subtitle (an address, usually)
// copies on click with visible feedback.
void kit_identity(const char* title, const char* subtitle,
    const char* hero = nullptr, float avatar_em = 2.4f,
    const char* copy_hint = nullptr, const char* copied_label = nullptr);

}
