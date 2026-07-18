#pragma once

#include <array>

#include "core/secure/secure_bytes.hpp"

namespace izan::ui {

// A password field that reports its focus into the page's secret-focus
// aggregate — the flag the IME detach rides on. Field-level, not
// form-level: ordinary text next to it keeps CJK input working. The
// hint shows inside the empty field; returns true when Enter submits.
bool secret_field(const char* label, std::array<char, 256>& buf,
    bool& secret_focus, const char* hint = nullptr);

// Moves the buffer contents into guarded memory and wipes the buffer.
secure::SecureBytes take_secret(std::array<char, 256>& buf);

}
