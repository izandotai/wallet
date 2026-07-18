#pragma once

#include <imgui.h>

namespace izan::ui {

// The dialog: one modal component so every dialog in the wallet is the
// same dialog. Anatomy and rhythm follow the macOS alert — a centered
// identity (avatar or glyph), a bold title, a quiet explanation, the
// content, then a pair of equal-width buttons with cancel on the left.
// The window itself carries the depth language: soft shadow (via the
// shell's popup shadow pass), a hairline border, a subtle top-light
// gradient wash. Escape dismisses; the caller wipes its buffers when
// told so.
//
// Usage:
//   kit_dialog_open("##rename");
//   ...
//   bool dismissed = false;
//   if (kit_dialog_begin("##rename", &dismissed)) {
//       if (dismissed) { /* wipe buffers */ }
//       kit_dialog_header_avatar(name, title, subtitle);
//       kit_dialog_field_width();
//       ImGui::InputText(...);
//       switch (kit_dialog_buttons(cancel, confirm)) { ... }
//       kit_dialog_end();
//   }

void kit_dialog_open(const char* id);

// true while the dialog is showing; *dismissed is set on the frame
// Escape closes it (the dialog is already closing — clean up).
bool kit_dialog_begin(const char* id, bool* dismissed = nullptr);
void kit_dialog_end();
void kit_dialog_close();

// Centered header: an identity avatar minted from a name, or an emoji
// glyph; then the title, then an optional wrapped explanation.
void kit_dialog_header_avatar(
    const char* avatar_name, const char* title, const char* subtitle = nullptr);
void kit_dialog_header_icon(
    const char* glyph, const char* title, const char* subtitle = nullptr);

// Sizes the next input to span the dialog's content width.
void kit_dialog_field_width();

// The footer pair, macOS order: cancel left, confirm right, equal
// widths across the row. Returns 0 = none, 1 = cancel, 2 = confirm.
// Neither closes the dialog — the caller decides.
int kit_dialog_buttons(const char* cancel, const char* confirm,
    bool confirm_enabled = true, bool destructive = false);

}
