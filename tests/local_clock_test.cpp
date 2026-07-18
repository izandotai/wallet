#include <doctest/doctest.h>

#include <cstdint>

#include "platform/time/local_clock.hpp"

TEST_CASE("the local clock answers a civil moment, not garbage")
{
    // 2026-05-02 14:19:00 UTC — any timezone must land within a day
    // of it and inside the world's ±14h offset band.
    const auto lm = izan::time::local_moment(1777731540ull);
    REQUIRE(lm.has_value());
    CHECK(lm->year == 2026);
    CHECK(lm->month == 5);
    CHECK((lm->day >= 1 && lm->day <= 3));
    CHECK((lm->hour >= 0 && lm->hour <= 23));
    CHECK((lm->minute >= 0 && lm->minute <= 59));
    CHECK((lm->second >= 0 && lm->second <= 59));
    CHECK((lm->offset_min >= -14 * 60 && lm->offset_min <= 14 * 60));
    // The offset is not an opinion: applying it to UTC must reproduce
    // the civil minute the platform reported.
    const std::int64_t utc_min = 1777731540ll / 60;
    const std::int64_t local_min = utc_min + lm->offset_min;
    CHECK(int(local_min % 60) == lm->minute);
    CHECK(int((local_min / 60) % 24) == lm->hour);
}
