#pragma once

#include <cstdint>
#include <optional>

namespace izan::time {

// A civil moment on the user's own clock, plus how far that clock
// sits from UTC at that instant. The OS is the timezone authority —
// each platform answers with whatever it trusts; on Windows that is
// the SYSTEMTIME conversion, DST rules included.
struct LocalMoment {
    int year = 0;
    int month = 0; // 1..12
    int day = 0;   // 1..31
    int hour = 0;
    int minute = 0;
    int second = 0;
    int offset_min = 0; // signed minutes east of UTC
};

std::optional<LocalMoment> local_moment(std::uint64_t unix_seconds);

}
