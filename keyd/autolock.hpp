#pragma once

#include <functional>
#include <string>

namespace izan::keyd {

// §3.1 hardening item 3: mlock pins pages in RAM, but suspend writes
// all of RAM to hiberfil.sys and a locked screen leaves the vault open
// for whoever sits down — so both events must wipe key material the
// moment they happen, not when the user returns.
//
// Starts a hidden top-level window on its own thread and reports
// session-lock and suspend through on_event. The window is
// deliberately findable (class "IzanKeydWatch", title = tag): tests
// post the notifications a real lock would deliver. A same-user
// process abusing that can only force a lock — the safe direction.
// Returns true once the window and the WTS session registration are
// both up; the caller folds that into the Hello hardening bitmask.
bool start_autolock_watch(
    const std::string& tag, std::function<void(const char*)> on_event);

}
