#pragma once

namespace izan::ui {

// A link that reads as a link: accent text, underline and hand cursor
// on hover, the system browser on click, the URL copied on
// right-click. Long labels middle-elide to the available width; the
// tooltip always carries the full URL for review before the jump.
void kit_hyperlink(const char* id, const char* label, const char* url);

}
