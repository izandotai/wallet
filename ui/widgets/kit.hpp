#pragma once

#include <imgui.h>

namespace izan::ui {

// The wallet's design kit: a small set of primitives that make imgui
// read like a native panel — type scale, inset grouped cards, capsule
// chips, identity avatars, one obvious primary action per screen.
// Everything derives from the active theme's palette so all shipped
// themes keep working; nothing here owns state.

// -- type scale ------------------------------------------------------
float kit_title_size();             // ~1.35x body, for screen headers
float kit_caption_size();           // ~0.85x body, for secondary lines

void kit_title(const char* text);
void kit_caption(const char* text); // muted color
void kit_vspace(float em = 0.5f);

// -- color -----------------------------------------------------------
ImVec4 kit_accent(); // the theme's accent (checkmark color)
ImVec4 kit_danger(); // destructive red, blended toward the theme

// -- containers ------------------------------------------------------
// Inset grouped card (System Settings style): rounded, slightly
// elevated, own padding. Height fits content.
void kit_group_begin(const char* id, float width = 0.0f);
void kit_group_end();
void kit_hairline(); // inset separator between rows of a group

// -- atoms -----------------------------------------------------------
// Identity avatar: rounded square, color minted from the name, the
// name's first character as the glyph.
void kit_avatar(const char* name, float size);
// Same, drawn at an absolute position without touching the cursor —
// for composing custom rows.
void kit_avatar_at(ImVec2 pos, const char* name, float size);

// Capsule chip: tinted background, full-strength text.
void kit_pill(const char* text, ImVec4 tint);

// -- controls --------------------------------------------------------
// The screen's one obvious action: accent-filled, rounded.
bool kit_primary_button(const char* label, float width = 0.0f);
// Quiet neighbor of a primary action: no fill until hovered.
bool kit_subtle_button(const char* label);
// Destructive: red fill, same shape as primary.
bool kit_danger_button(const char* label, float width = 0.0f);

}
