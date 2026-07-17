#include <doctest/doctest.h>

#include <cstring>
#include <utility>

#include "core/secure/secure_bytes.hpp"

using izan::secure::SecureBytes;

TEST_CASE("guarded allocation round-trips bytes")
{
    SecureBytes buf(32);
    REQUIRE(buf.size() == 32);
    REQUIRE(!buf.empty());
    for (std::size_t i = 0; i < buf.size(); ++i)
        buf.data()[i] = static_cast<uint8_t>(i);
    for (std::size_t i = 0; i < buf.size(); ++i)
        CHECK(buf.data()[i] == static_cast<uint8_t>(i));
}

TEST_CASE("default and zero-size buffers hold nothing")
{
    SecureBytes none;
    CHECK(none.empty());
    CHECK(none.data() == nullptr);

    SecureBytes zero(0);
    CHECK(zero.empty());
    CHECK(zero.data() == nullptr);
}

TEST_CASE("move transfers ownership and empties the source")
{
    SecureBytes a(16);
    std::memset(a.data(), 0xab, a.size());
    const uint8_t* raw = a.data();

    SecureBytes b(std::move(a));
    CHECK(b.data() == raw);
    CHECK(b.size() == 16);
    CHECK(a.data() == nullptr);
    CHECK(a.empty());

    SecureBytes c;
    c = std::move(b);
    CHECK(c.data() == raw);
    CHECK(b.data() == nullptr);
    CHECK(c.data()[15] == 0xab);
}

TEST_CASE("protect then Access reopens the pages")
{
    SecureBytes buf(8);
    std::memset(buf.data(), 0x5a, buf.size());
    buf.protect();
    {
        SecureBytes::Access open(buf);
        CHECK(buf.data()[0] == 0x5a);
        buf.data()[0] = 0xa5;
    }
    // Pages are no-access again here; reopen to observe the write.
    {
        SecureBytes::Access open(buf);
        CHECK(buf.data()[0] == 0xa5);
        CHECK(buf.data()[7] == 0x5a);
    }
}

TEST_CASE("Access on an unprotected buffer is a no-op")
{
    SecureBytes buf(4);
    buf.data()[0] = 1;
    {
        SecureBytes::Access open(buf);
        CHECK(buf.data()[0] == 1);
    }
    // Destructor must not have locked a buffer it found unlocked.
    CHECK(buf.data()[0] == 1);
}

TEST_CASE("ct_equal compares content in constant time")
{
    SecureBytes a(4);
    SecureBytes b(4);
    std::memset(a.data(), 0x11, 4);
    std::memset(b.data(), 0x11, 4);
    CHECK(a.ct_equal(b));

    b.data()[3] = 0x12;
    CHECK(!a.ct_equal(b));

    SecureBytes shorter(3);
    std::memset(shorter.data(), 0x11, 3);
    CHECK(!a.ct_equal(shorter));

    SecureBytes empty1;
    SecureBytes empty2;
    CHECK(empty1.ct_equal(empty2));
    CHECK(!empty1.ct_equal(a));
}

TEST_CASE("reset releases and empties")
{
    SecureBytes buf(64);
    buf.protect();
    buf.reset();
    CHECK(buf.empty());
    CHECK(buf.data() == nullptr);
    // Safe to call twice.
    buf.reset();
}
