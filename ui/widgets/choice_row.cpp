#include "ui/widgets/choice_row.hpp"

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

namespace {

    void draw_mark(ImDrawList* draw, ImVec2 center, float em, bool selected)
    {
        const float r = em * 0.28f;
        if (selected) {
            draw->AddCircleFilled(center, r, ImGui::GetColorU32(kit_accent()));
            // The check is drawn, not typed — a "✓" glyph would come
            // out of the emoji font, colored and oversized.
            const ImVec2 tick[3] = {
                ImVec2(center.x - r * 0.50f, center.y + r * 0.05f),
                ImVec2(center.x - r * 0.10f, center.y + r * 0.45f),
                ImVec2(center.x + r * 0.55f, center.y - r * 0.40f),
            };
            draw->AddPolyline(tick, 3, IM_COL32(255, 255, 255, 245),
                ImDrawFlags_RoundCornersAll, r * 0.28f);
        } else {
            draw->AddCircle(
                center, r, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f));
        }
    }

}

bool kit_choice_row(const char* id, const char* label,
    const char* trailing_caption, bool selected)
{
    const float em = ImGui::GetFontSize();
    const float row_h = em * 1.4f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(row_w, row_h));
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw_mark(
        draw, ImVec2(pos.x + em * 0.4f, pos.y + row_h * 0.5f), em, selected);
    draw->AddText(ImVec2(pos.x + em * 1.1f, pos.y + (row_h - em) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text), label);
    if (trailing_caption && *trailing_caption) {
        ImGui::PushFont(nullptr, kit_caption_size());
        const float w = ImGui::CalcTextSize(trailing_caption).x;
        const float x = pos.x + row_w - w;
        draw->AddText(ImGui::GetFont(), kit_caption_size(),
            ImVec2(x, pos.y + (row_h - kit_caption_size()) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), trailing_caption);
        ImGui::PopFont();
    }
    return clicked;
}

bool kit_selection_mark(const char* id, bool selected)
{
    // Sized to the frame height so the mark centers against framed
    // neighbors (input fields) on the same row.
    const float em = ImGui::GetFontSize();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(em * 1.2f, height));
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw_mark(ImGui::GetWindowDrawList(),
        ImVec2(pos.x + em * 0.45f, pos.y + height * 0.5f), em, selected);
    return clicked;
}

}
