#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace izan::i18n {

// One flat JSON object per language in assets/lang/<code>.json.
// English is the base: the requested language is laid over it, so a
// hole in a translation shows English instead of nothing — and the
// hole is reported, because shipped catalogs are not allowed to have
// any (the test suite enforces zero missing and zero orphan keys).
inline constexpr const char* kBaseLanguage = "en";
// Every catalog names itself for the language menu.
inline constexpr const char* kNameKey = "lang.name";

class Catalog {
public:
    // Throws when the base catalog or the requested one is absent or
    // malformed — a wallet must not limp along on half a language.
    static Catalog load(
        const std::filesystem::path& dir, std::string_view language);

    // Language codes with a catalog file present, base first.
    static std::vector<std::string> available(const std::filesystem::path& dir);

    // Pages pass literal keys; an unknown key comes back verbatim,
    // which is exactly the marker a reviewer needs to see on screen.
    const char* operator()(const char* key) const;

    const std::string& language() const
    {
        return m_language;
    }

    // Keys the requested language lacks (English shown instead).
    const std::vector<std::string>& missing() const
    {
        return m_missing;
    }

    // Keys the requested language has but the base does not — always a
    // typo on one side.
    const std::vector<std::string>& orphans() const
    {
        return m_orphans;
    }

private:
    std::string m_language;
    std::map<std::string, std::string, std::less<>> m_strings;
    std::vector<std::string> m_missing;
    std::vector<std::string> m_orphans;
};

}
