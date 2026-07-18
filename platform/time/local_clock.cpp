#include "platform/time/local_clock.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace izan::time {

std::optional<LocalMoment> local_moment(std::uint64_t unix_seconds)
{
    const std::uint64_t ticks
        = unix_seconds * 10000000ull + 116444736000000000ull;
    const FILETIME utc_ft { DWORD(ticks & 0xffffffffull), DWORD(ticks >> 32) };
    SYSTEMTIME utc {}, local {};
    if (!FileTimeToSystemTime(&utc_ft, &utc)
        || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return std::nullopt;
    FILETIME local_ft {};
    if (!SystemTimeToFileTime(&local, &local_ft))
        return std::nullopt;
    const std::int64_t local_ticks
        = std::int64_t((std::uint64_t(local_ft.dwHighDateTime) << 32)
            | local_ft.dwLowDateTime);
    LocalMoment out;
    out.year = local.wYear;
    out.month = local.wMonth;
    out.day = local.wDay;
    out.hour = local.wHour;
    out.minute = local.wMinute;
    out.second = local.wSecond;
    out.offset_min = int((local_ticks - std::int64_t(ticks)) / 600000000ll);
    return out;
}

}
