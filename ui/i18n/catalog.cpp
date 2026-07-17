#include "ui/i18n/catalog.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <glaze/glaze.hpp>

namespace izan::i18n {

namespace {

    std::map<std::string, std::string, std::less<>> read_catalog_file(
        const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("i18n: missing catalog " + path.string());
        std::ostringstream buf;
        buf << f.rdbuf();
        std::map<std::string, std::string, std::less<>> out;
        if (glz::read_json(out, buf.str()))
            throw std::runtime_error(
                "i18n: malformed catalog " + path.string());
        return out;
    }

}

Catalog Catalog::load(
    const std::filesystem::path& dir, std::string_view language)
{
    Catalog cat;
    cat.m_language = std::string(language);
    cat.m_strings
        = read_catalog_file(dir / (std::string(kBaseLanguage) + ".json"));

    if (language != kBaseLanguage) {
        const auto overlay
            = read_catalog_file(dir / (cat.m_language + ".json"));
        for (auto& [key, text] : cat.m_strings) {
            const auto it = overlay.find(key);
            if (it == overlay.end())
                cat.m_missing.push_back(key);
            else
                text = it->second;
        }
        for (const auto& [key, text] : overlay)
            if (!cat.m_strings.contains(key))
                cat.m_orphans.push_back(key);
    }
    return cat;
}

std::vector<std::string> Catalog::available(const std::filesystem::path& dir)
{
    std::vector<std::string> codes;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.path().extension() == ".json")
            codes.push_back(entry.path().stem().string());
    std::sort(codes.begin(), codes.end(),
        [](const std::string& a, const std::string& b) {
            if (a == kBaseLanguage)
                return true;
            if (b == kBaseLanguage)
                return false;
            return a < b;
        });
    return codes;
}

const char* Catalog::operator()(const char* key) const
{
    const auto it = m_strings.find(std::string_view(key));
    return it == m_strings.end() ? key : it->second.c_str();
}

}
