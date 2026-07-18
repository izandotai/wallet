#pragma once

#include <string>

namespace izan::ui {

// Text roles. The language's type scale in wearable form: a screen has
// one title, dialogs get headings, secondary lines are captions.

float kit_title_size();
float kit_heading_size();
float kit_caption_size();

void kit_title(const char* text);
void kit_heading(const char* text);
void kit_caption(const char* text); // muted color
void kit_vspace(float em = 0.5f);   // vertical breath between blocks

// Middle elision to a pixel budget: keep both ends — the parts a
// person actually compares — and give up the middle. ASCII-safe
// slicing. font_size = 0 measures at the current size; pass an
// explicit size when the result will be drawn at one — measuring in
// one font context and drawing in another is how text goes ragged.
std::string kit_elide_middle(
    const char* text, float budget, float font_size = 0.0f);

// End elision for names and titles: keep the head, drop the tail.
// UTF-8 aware — cuts only on codepoint boundaries, CJK-safe.
std::string kit_elide_end(
    const char* text, float budget, float font_size = 0.0f);

// A caption that never breaks its column: end-elided to the budget,
// with the full text served as a tooltip when shortened — raw error
// strings from other machines get long, and long must stay readable.
void kit_caption_fit(const char* text, float budget);

// Fine print: a muted, oblique, word-wrapped block for machine-made
// messages — node errors, diagnostics. The slant is sheared onto the
// glyphs at draw time; no italic file lives in the font waterfall.
// Draws at the cursor, wraps to `width`, advances the layout.
void kit_footnote(const char* text, float width);

}
