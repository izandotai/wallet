#include "ui/widgets/label.hpp"

#include <cstring>

#include "ui/widgets/design.hpp"

namespace izan::ui {

float kit_title_size()
{
    return kit_snap(ImGui::GetFontSize() * design().title_scale);
}

float kit_heading_size()
{
    return kit_snap(ImGui::GetFontSize() * design().heading_scale);
}

float kit_caption_size()
{
    return kit_snap(ImGui::GetFontSize() * design().caption_scale);
}

void kit_title(const char* text)
{
    ImGui::PushFont(nullptr, kit_title_size());
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void kit_heading(const char* text)
{
    ImGui::PushFont(nullptr, kit_heading_size());
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

std::string kit_elide_middle(const char* text, float budget, float font_size)
{
    const auto measure = [&](const char* s) {
        if (font_size > 0.0f)
            return ImGui::GetFont()
                ->CalcTextSizeA(font_size, FLT_MAX, 0.0f, s)
                .x;
        return ImGui::CalcTextSize(s).x;
    };
    if (measure(text) <= budget)
        return text;
    const std::size_t len = std::strlen(text);
    const std::size_t tail = len < 6 ? len : 6;
    for (std::size_t head = len; head > 4; --head) {
        std::string out(text, head);
        out += "…";
        out.append(text + len - tail, tail);
        if (measure(out.c_str()) <= budget)
            return out;
    }
    std::string out(text, 4);
    out += "…";
    out.append(text + len - tail, tail);
    return out;
}

}
