#pragma once

namespace izan::ui {

// Buttons. Theme-default metrics always — only color separates the
// ranks, so any pair of buttons sits at the same height.

// The screen's one obvious action: accent-filled.
bool kit_primary_button(const char* label, float width = 0.0f);
// Quiet neighbor of a primary action: the theme's own button.
bool kit_subtle_button(const char* label, float width = 0.0f);
// Destructive: red fill, same shape as primary.
bool kit_danger_button(const char* label, float width = 0.0f);
// An action that reads as text: accent-colored label, no fill until
// hovered — for "add another" rows and friends.
bool kit_link_button(const char* label);

// The width a kit button will actually occupy — centering math must
// ask this, not CalcTextSize: the auto-width floor makes the render
// wider than the label.
float kit_button_width(const char* label, float width = 0.0f);

}
