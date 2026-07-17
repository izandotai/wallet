#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "ui/i18n/catalog.hpp"

using izan::i18n::Catalog;

namespace {

const std::filesystem::path kLangDir
    = std::filesystem::path(IZAN_SOURCE_DIR) / "ui" / "assets" / "lang";

std::filesystem::path temp_lang_dir()
{
    const auto dir = std::filesystem::temp_directory_path() / "izan_i18n_test";
    std::filesystem::create_directories(dir);
    return dir;
}

void write_file(const std::filesystem::path& p, const char* text)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

}

TEST_CASE("i18n: base catalog lookups, unknown key returns the key")
{
    Catalog en = Catalog::load(kLangDir, "en");
    CHECK(std::strcmp(en("vault.unlock"), "Unlock") == 0);
    CHECK(std::strcmp(en("lang.name"), "English") == 0);
    // The verbatim key on screen is the intended missing-string marker.
    CHECK(std::strcmp(en("no.such.key"), "no.such.key") == 0);
    CHECK(en.missing().empty());
    CHECK(en.orphans().empty());
}

TEST_CASE("i18n: every shipped language is complete against the base")
{
    const auto codes = Catalog::available(kLangDir);
    REQUIRE(codes.size() == 8);
    CHECK(codes.front() == "en"); // base sorts first

    for (const std::string& code : codes) {
        Catalog cat = Catalog::load(kLangDir, code);
        INFO("language: ", code);
        // Shipping a hole or a typo key is a build failure, not a
        // runtime surprise — this is the no-retrofit-translations rule
        // with teeth.
        CHECK(cat.missing().empty());
        CHECK(cat.orphans().empty());
        CHECK(cat("lang.name")[0] != '\0');
        CHECK(std::strcmp(cat("lang.name"), "lang.name") != 0);
    }
}

TEST_CASE("i18n: partial translation falls back to English and reports")
{
    const auto dir = temp_lang_dir();
    write_file(
        dir / "en.json", R"({"lang.name":"English","a":"Alpha","b":"Beta"})");
    write_file(dir / "xx.json", R"({"lang.name":"Xx","a":"AlphaXx","z":"?"})");

    Catalog xx = Catalog::load(dir, "xx");
    CHECK(std::strcmp(xx("a"), "AlphaXx") == 0);
    CHECK(std::strcmp(xx("b"), "Beta") == 0); // hole → English
    REQUIRE(xx.missing().size() == 1);
    CHECK(xx.missing()[0] == "b");
    REQUIRE(xx.orphans().size() == 1);
    CHECK(xx.orphans()[0] == "z");

    std::filesystem::remove_all(dir);
}

TEST_CASE("i18n: absent or malformed catalogs fail loudly")
{
    const auto dir = temp_lang_dir();
    CHECK_THROWS(Catalog::load(dir, "en")); // no base at all

    write_file(dir / "en.json", R"({"lang.name":"English"})");
    CHECK_THROWS(Catalog::load(dir, "zz")); // requested language absent

    write_file(dir / "bad.json", "not json at all");
    CHECK_THROWS(Catalog::load(dir, "bad"));

    std::filesystem::remove_all(dir);
}
