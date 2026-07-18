#include "ui/widgets/choice_row.hpp"

#include <string>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

namespace {

    // The macOS radio: a quiet recessed well when idle, the accent
    // disc with a white center dot when chosen. No glyphs, no ticks —
    // one filled circle says "this one" better than any drawing.
    void draw_mark(ImDrawList* draw, ImVec2 center, float em, bool selected)
    {
        const float r = em * 0.32f;
        if (selected) {
            draw->AddCircleFilled(center, r, ImGui::GetColorU32(kit_accent()));
            draw->AddCircleFilled(
                center, r * 0.38f, IM_COL32(255, 255, 255, 245));
            draw->AddCircle(center, r, IM_COL32(0, 0, 0, 40));
        } else {
            const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            draw->AddCircleFilled(center, r,
                ImGui::GetColorU32(
                    kit_blend(bg, text, kit_is_dark() ? 0.07f : 0.05f)));
            draw->AddCircle(center, r,
                ImGui::GetColorU32(
                    ImGui::GetStyleColorVec4(ImGuiCol_Separator)));
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
    const float label_x = pos.x + em * 1.1f;
    draw->AddText(
        ImVec2(kit_snap(label_x), kit_snap(pos.y + (row_h - em) * 0.5f)),
        ImGui::GetColorU32(ImGuiCol_Text), label);
    if (trailing_caption && *trailing_caption) {
        // The caption keeps clear of the label — elided before it may
        // ever overlap, with a breath of space guaranteed between.
        // Measured and drawn at one explicit size: mixing font
        // contexts is how right edges go ragged.
        const float cap = kit_caption_size();
        const float label_end = label_x + ImGui::CalcTextSize(label).x;
        const float budget = pos.x + row_w - label_end - em * 0.8f;
        const std::string text = budget > 0.0f
            ? kit_elide_middle(trailing_caption, budget, cap)
            : "";
        const float w = ImGui::GetFont()
                            ->CalcTextSizeA(cap, FLT_MAX, 0.0f, text.c_str())
                            .x;
        draw->AddText(ImGui::GetFont(), cap,
            ImVec2(kit_snap(pos.x + row_w - w),
                kit_snap(pos.y + (row_h - cap) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
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
