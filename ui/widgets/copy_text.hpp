#pragma once

namespace izan::ui {

// Click-to-copy text with visible feedback: copies `full` on click,
// then swaps to the confirmation for a moment so the copy is never
// silent. Display is the component's business — when the row runs
// tight the text middle-elides to fit, keeping both ends recognizable.
// Hovering shows the full text plus the hint.
void kit_copy_text(const char* id, const char* full, const char* hint,
    const char* copied_label);

// Same, right-aligned to the current content region's edge — for the
// trailing column of a row. reserve_right_em keeps room for widgets
// that trail even further right (a QR button, an action).
void kit_copy_text_right(const char* id, const char* full, const char* hint,
    const char* copied_label, float reserve_right_em = 0.0f);

// Same, centered in the current content region — for dialogs built on
// a center axis.
void kit_copy_text_centered(const char* id, const char* full, const char* hint,
    const char* copied_label);

}
