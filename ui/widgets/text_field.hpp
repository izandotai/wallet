#pragma once

#include <array>
#include <cstddef>

#include <imgui.h>

#include "core/secure/secure_bytes.hpp"

namespace izan::ui {

// Text entry. Every field shows its purpose as a hint inside the empty
// box and submits on Enter.

// Ordinary text; returns true on Enter.
bool kit_text_field(
    const char* id, const char* hint, char* buf, std::size_t size);

// The field dress, exported for kit siblings that build richer entries
// (amount row, address row) and must wear the same clothes.
void kit_field_style_push();
void kit_field_style_pop();

// The dress painted as a bare rectangle — for composite fields that
// place a narrower, frameless input inside a wider frame so text and
// caret stop before the trailing controls instead of running under
// them.
void kit_field_frame(const ImVec2& pos, const ImVec2& size);

// A password field that reports its focus into the page's secret-focus
// aggregate — the flag the IME detach rides on. Field-level, not
// form-level: ordinary text next to it keeps CJK input working. The
// hint shows inside the empty field; returns true when Enter submits.
bool secret_field(const char* label, std::array<char, 256>& buf,
    bool& secret_focus, const char* hint = nullptr);

// The paste box for secrets-in-transit (mnemonics, keys): a rounded
// text area with its own quiet fill and hairline border, a hint while
// empty, IME-detached while focused. Returns true when the text
// changed.
bool kit_paste_box(const char* id, const char* hint, char* buf,
    std::size_t size, float rows, bool& secret_focus);

// Moves the buffer contents into guarded memory and wipes the buffer.
secure::SecureBytes take_secret(std::array<char, 256>& buf);

// Programmatic focus for the next field — without the keyboard-nav
// cursor ring that SetKeyboardFocusHere would paint around it.
void kit_focus_here();

}
