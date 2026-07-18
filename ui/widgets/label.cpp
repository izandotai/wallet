#include "ui/widgets/label.hpp"

#include <cstring>
#include <vector>

#include "ui/widgets/design.hpp"
#include "ui/widgets/tooltip.hpp"

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

namespace {

    float measure_at(const char* s, float font_size)
    {
        if (font_size > 0.0f)
            return ImGui::GetFont()
                ->CalcTextSizeA(font_size, FLT_MAX, 0.0f, s)
                .x;
        return ImGui::CalcTextSize(s).x;
    }

}

std::string kit_elide_middle(const char* text, float budget, float font_size)
{
    const auto measure
        = [&](const char* s) { return measure_at(s, font_size); };
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
    // Even the minimal head-and-tail pair overflows: degrade to a
    // shrinking head, and to a bare ellipsis at the bitter end — the
    // component never draws wider than its slot.
    for (std::size_t head = 4; head > 0; --head) {
        std::string out(text, head);
        out += "…";
        if (measure(out.c_str()) <= budget)
            return out;
    }
    return "…";
}

std::string kit_elide_end(const char* text, float budget, float font_size)
{
    if (measure_at(text, font_size) <= budget)
        return text;
    // Codepoint start offsets, so the cut never lands inside a
    // multibyte character.
    std::vector<std::size_t> starts;
    for (const char* c = text; *c; ++c)
        if ((*c & 0xC0) != 0x80)
            starts.push_back(std::size_t(c - text));
    for (std::size_t n = starts.size(); n > 1; --n) {
        std::string out(text, starts[n - 1]);
        out += "…";
        if (measure_at(out.c_str(), font_size) <= budget)
            return out;
    }
    return "…";
}

void kit_caption_fit(const char* text, float budget)
{
    ImGui::PushFont(nullptr, kit_caption_size());
    const std::string shown = kit_elide_end(text, budget, kit_caption_size());
    ImGui::TextDisabled("%s", shown.c_str());
    if (shown != text && ImGui::IsItemHovered())
        kit_tooltip(text);
    ImGui::PopFont();
}

void kit_footnote(const char* text, float width)
{
    const float size = kit_caption_size();
    ImFont* font = ImGui::GetFont();
    const float line_h = kit_snap(size * 1.45f);
    const float slant = 0.18f;
    const float wrap_w = width - size * slant;
    if (wrap_w <= size)
        return;

    auto width_of = [&](const std::string& s) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str()).x;
    };
    // Greedy word wrap; an unbroken run — a hex address — splits by
    // character, cutting only on UTF-8 codepoint boundaries.
    std::vector<std::string> lines;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) {
            lines.push_back(cur);
            cur.clear();
        }
    };
    const std::string all(text);
    std::size_t i = 0;
    while (i < all.size()) {
        std::size_t j = all.find(' ', i);
        if (j == std::string::npos)
            j = all.size();
        std::string word = all.substr(i, j - i);
        i = j + 1;
        while (width_of(word) > wrap_w) {
            flush();
            std::size_t k = word.size();
            while (k > 1
                && (width_of(word.substr(0, k)) > wrap_w
                    || (word[k] & 0xC0) == 0x80))
                --k;
            lines.push_back(word.substr(0, k));
            word = word.substr(k);
        }
        const std::string cand = cur.empty() ? word : cur + " " + word;
        if (width_of(cand) <= wrap_w)
            cur = cand;
        else {
            flush();
            cur = word;
        }
    }
    flush();
    if (lines.empty())
        return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, float(lines.size()) * line_h));
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    float y = origin.y;
    for (const std::string& line : lines) {
        const int v0 = draw->VtxBuffer.Size;
        draw->AddText(font, size, ImVec2(kit_snap(origin.x), kit_snap(y)),
            color, line.c_str());
        // The oblique: shear every glyph vertex around the line's
        // baseline — the top of each letter leans right.
        const float base = y + size;
        for (int v = v0; v < draw->VtxBuffer.Size; ++v) {
            ImDrawVert& vert = draw->VtxBuffer.Data[v];
            vert.pos.x += (base - vert.pos.y) * slant;
        }
        y += line_h;
    }
}

}
