#pragma once

#include <imgui.h>

namespace izan::ui {

// The design language: every geometric and material decision the UI
// makes, in one place. Color themes say WHAT colors; the design
// language says HOW things are shaped, spaced and lit — type scale,
// rhythm, radii, shadows, washes. Components consume these tokens and
// never hardcode their own numbers, so changing the app's character
// means editing (or swapping) a language, not touching every view.
//
// Lengths are in em (multiples of the base font size) unless noted;
// alphas are 0–255.
struct DesignLanguage {
    // -- type scale (multipliers of the body size) --
    float title_scale = 1.35f;   // screen headers
    float heading_scale = 1.15f; // dialog titles
    float caption_scale = 0.85f; // secondary lines

    // -- rhythm --
    float pane_pad_x = 1.1f;     // detail pane padding
    float pane_pad_y = 0.9f;
    float sidebar_pad = 0.5f;    // list pane padding
    float dialog_pad_x = 1.1f;   // dialog window padding
    float dialog_pad_y = 0.95f;
    float dialog_row_gap = 0.5f; // item spacing inside dialogs
    float group_pad_x = 0.6f;    // inset grouped card padding
    float group_pad_y = 0.45f;

    // -- shape --
    float dialog_radius = 0.45f;    // the dialog window's corners
    float selection_radius = 0.35f; // rounded selection highlights
    float avatar_radius = 0.28f;    // × avatar size (squircle)

    // -- component metrics --
    float dialog_width = 15.0f; // one width for every dialog
    float form_width = 14.0f;   // create/import/unlock field columns
    float sidebar_width = 12.0f;
    float list_row_height = 2.4f;
    float list_avatar = 1.7f;
    float header_avatar = 2.1f; // detail-pane header
    float dialog_avatar = 2.4f; // dialog identity
    float dialog_glyph = 2.0f;  // dialog emoji icon
    float lock_avatar = 3.0f;   // the lock screen's centerpiece

    // -- buttons --
    // Shape and finish are tokens, not code: flip these to restyle
    // every button in the app at once.
    bool button_pill = true;      // capsule ends (full-round sides)
    float button_gloss = 0.55f;   // 0..1 metallic top-light overlay
    float button_min_em = 4.5f;   // auto-width floor: no squat capsules
    float button_pad_x_em = 1.1f; // pill text inset — round ends need air

    // -- material & light --
    float group_elevation_dark = 0.045f; // card bg lift toward text
    float group_elevation_light = 0.03f;
    float sidebar_recess = 0.82f;        // sidebar bg multiplier
    bool dialog_wash = true;             // top-light gradient (dark)
    float wash_height = 3.2f;
    int wash_alpha = 10;
    int rim_alpha = 26;                  // 1px inner top highlight

    // -- identity avatars --
    float avatar_glyph = 0.52f; // glyph size × avatar size
    float avatar_sat = 0.52f;   // minted hue's saturation
    float avatar_val = 0.72f;   // and value
};

// The active language. Ships as Cupertino; more languages are new
// definitions, not new component code.
const DesignLanguage& design();
void set_design(const DesignLanguage& language);
DesignLanguage design_cupertino();

// Palette derivation — how the language turns the color theme's
// swatches into component colors. Components use these, never raw mixes.
ImVec4 kit_blend(const ImVec4& a, const ImVec4& b, float t);
bool kit_is_dark();  // does the active theme read as dark
ImVec4 kit_accent(); // the theme's accent (checkmark color)
ImVec4 kit_danger(); // destructive red, blended toward the theme

// Whole pixels only: font sizes and hand-drawn text positions snap to
// the grid, or FreeType renders them soft.
inline float kit_snap(float v)
{
    return float(int(v + 0.5f));
}

// Returns the layout cursor to `keep` after an overlay detour — the
// imgui-legal way. A bare SetCursorScreenPos at the end of a widget
// trips the "submit an item after moving the cursor" debug check, so
// the position is re-asserted through a zero-size item (offset one
// item-spacing up, which the zero item's own spacing puts back).
inline void kit_cursor_restore(const ImVec2& keep)
{
    ImGui::SetCursorScreenPos(
        ImVec2(keep.x, keep.y - ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}
