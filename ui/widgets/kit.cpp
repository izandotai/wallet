#include "ui/widgets/kit.hpp"

#include <cstdint>
#include <cstring>

#include "ui/widgets/design.hpp"

namespace izan::ui {

namespace {

    ImVec4 blend(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    // How much luminance the theme's window background carries decides
    // whether "elevated" means lighter or darker.
    bool dark_theme()
    {
        const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        return 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z < 0.5f;
    }

}

float kit_title_size()
{
    return ImGui::GetFontSize() * design().title_scale;
}

float kit_caption_size()
{
    return ImGui::GetFontSize() * design().caption_scale;
}

void kit_title(const char* text)
{
    ImGui::PushFont(nullptr, kit_title_size());
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void kit_caption(const char* text)
{
    ImGui::PushFont(nullptr, kit_caption_size());
    ImGui::TextDisabled("%s", text);
    ImGui::PopFont();
}

void kit_vspace(float em)
{
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * em));
}

ImVec4 kit_accent()
{
    return ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
}

ImVec4 kit_danger()
{
    // A firm red, nudged toward the theme's text so it sits in-palette.
    return blend(ImVec4(0.86f, 0.26f, 0.24f, 1.0f),
        ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.08f);
}

void kit_group_begin(const char* id, float width)
{
    const DesignLanguage& dl = design();
    const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        blend(base, text,
            dark_theme() ? dl.group_elevation_dark : dl.group_elevation_light));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(ImGui::GetFontSize() * dl.group_pad_x,
            ImGui::GetFontSize() * dl.group_pad_y));
    ImGui::BeginChild(id, ImVec2(width, 0.0f),
        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void kit_group_end()
{
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void kit_hairline()
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y),
        ImVec2(pos.x + width, pos.y),
        ImGui::GetColorU32(ImGuiCol_Separator, 0.45f));
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.5f));
}

void kit_avatar_at(ImVec2 pos, const char* name, float size)
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
    const float glyph_size = size * dl.avatar_glyph;
    ImGui::PushFont(nullptr, glyph_size);
    const ImVec2 text_size = ImGui::CalcTextSize(name, end);
    draw->AddText(ImGui::GetFont(), glyph_size,
        ImVec2(pos.x + (size - text_size.x) * 0.5f,
            pos.y + (size - text_size.y) * 0.5f),
        IM_COL32(255, 255, 255, 235), name, end);
    ImGui::PopFont();
}

void kit_avatar(const char* name, float size)
{
    kit_avatar_at(ImGui::GetCursorScreenPos(), name, size);
    ImGui::Dummy(ImVec2(size, size));
}

void kit_pill(const char* text, ImVec4 tint)
{
    ImGui::PushFont(nullptr, kit_caption_size());
    const ImVec2 label = ImGui::CalcTextSize(text);
    const float pad_x = ImGui::GetFontSize() * 0.55f;
    const float pad_y = ImGui::GetFontSize() * 0.18f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(label.x + pad_x * 2.0f, label.y + pad_y * 2.0f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec4 bg = tint;
    bg.w = 0.16f;
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        ImGui::GetColorU32(bg), size.y * 0.5f);
    draw->AddText(
        ImVec2(pos.x + pad_x, pos.y + pad_y), ImGui::GetColorU32(tint), text);
    ImGui::Dummy(size);
    ImGui::PopFont();
}

namespace {

    // Theme-default size and rounding; only the fill sets it apart, so
    // a primary and its quiet neighbor always sit at the same height.
    bool filled_button(const char* label, float width, const ImVec4& fill)
    {
        const ImVec4 hover = blend(fill, ImVec4(1, 1, 1, fill.w), 0.12f);
        const ImVec4 active = blend(fill, ImVec4(0, 0, 0, fill.w), 0.12f);
        ImGui::PushStyleColor(ImGuiCol_Button, fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.96f));
        const bool clicked = ImGui::Button(label, ImVec2(width, 0.0f));
        ImGui::PopStyleColor(4);
        return clicked;
    }

}

bool kit_primary_button(const char* label, float width)
{
    ImVec4 accent = kit_accent();
    accent.w = 1.0f;
    return filled_button(label, width, accent);
}

bool kit_danger_button(const char* label, float width)
{
    return filled_button(label, width, kit_danger());
}

bool kit_subtle_button(const char* label)
{
    // The theme's own button: same size, same shape as a primary —
    // only the accent fill tells them apart.
    return ImGui::Button(label);
}

}
