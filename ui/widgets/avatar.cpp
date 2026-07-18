#include "ui/widgets/avatar.hpp"

#include <cstdint>

#include "ui/widgets/design.hpp"

namespace izan::ui {

ImVec4 kit_identity_color(const char* name)
{
    const DesignLanguage& dl = design();
    // Mint a stable hue from the name; saturation and value stay in a
    // friendly band so every wallet gets a distinct but calm color.
    unsigned hash = 2166136261u;
    for (const char* c = name; *c; ++c)
        hash = (hash ^ uint8_t(*c)) * 16777619u;
    ImVec4 color;
    ImGui::ColorConvertHSVtoRGB(float(hash % 360u) / 360.0f, dl.avatar_sat,
        dl.avatar_val, color.x, color.y, color.z);
    color.w = 1.0f;
    return color;
}

void kit_avatar_at(ImVec2 pos, const char* name, float size)
{
    const DesignLanguage& dl = design();
    const ImVec4 color = kit_identity_color(name);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
        ImGui::GetColorU32(color), size * dl.avatar_radius);

    // The first UTF-8 character of the name, centered in white.
    const char* end = name;
    if (*end) {
        ++end;
        while ((*end & 0xC0) == 0x80)
            ++end;
    }
    const float glyph_size = kit_snap(size * dl.avatar_glyph);
    ImGui::PushFont(nullptr, glyph_size);
    const ImVec2 text_size = ImGui::CalcTextSize(name, end);
    draw->AddText(ImGui::GetFont(), glyph_size,
        ImVec2(kit_snap(pos.x + (size - text_size.x) * 0.5f),
            kit_snap(pos.y + (size - text_size.y) * 0.5f)),
        IM_COL32(255, 255, 255, 235), name, end);
    ImGui::PopFont();
}

void kit_avatar(const char* name, float size)
{
    kit_avatar_at(ImGui::GetCursorScreenPos(), name, size);
    ImGui::Dummy(ImVec2(size, size));
}

}
