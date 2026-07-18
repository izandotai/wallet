#include "ui/widgets/asset_row.hpp"

#include <cfloat>
#include <string>

#include <imgui.h>

#include "ui/widgets/avatar.hpp"
#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

AssetRowEvent kit_asset_row(const char* id, const char* symbol,
    const char* chain, const char* balance, bool ok, const char* error_note,
    const char* fiat, bool with_menu)
{
    AssetRowEvent ev;
    const float em = ImGui::GetFontSize();
    const float row_h = em * 2.3f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    // The dots keep their own slot so the numbers never sit under a
    // button — the field-frame lesson, applied to a row.
    const float slot = with_menu ? em * 1.5f : 0.0f;
    const float body_w = row_w - slot;
    ImGui::PushID(id);
    ev.clicked = ImGui::InvisibleButton("##row", ImVec2(body_w, row_h));
    ev.hovered = ImGui::IsItemHovered();
    if (with_menu && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        ev.menu = true;
    bool dots_hover = false;
    if (with_menu) {
        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::InvisibleButton("##dots", ImVec2(slot, row_h)))
            ev.menu = true;
        dots_hover = ImGui::IsItemHovered();
    }
    ImGui::PopID();
    if (ev.hovered || dots_hover)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (ev.hovered || dots_hover) {
        const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        draw->AddRectFilled(ImVec2(pos.x - em * 0.3f, pos.y),
            ImVec2(pos.x + row_w + em * 0.3f, pos.y + row_h),
            ImGui::GetColorU32(
                kit_blend(bg, text, kit_is_dark() ? 0.06f : 0.045f)),
            ImGui::GetFontSize() * design().selection_radius);
    }
    const float avatar = em * 1.6f;
    kit_avatar_at(
        ImVec2(pos.x, pos.y + (row_h - avatar) * 0.5f), symbol, avatar);

    const float text_x = kit_snap(pos.x + avatar + em * 0.5f);
    draw->AddText(ImVec2(text_x, kit_snap(pos.y + em * 0.2f)),
        ImGui::GetColorU32(ImGuiCol_Text), symbol);
    draw->AddText(ImGui::GetFont(), kit_caption_size(),
        ImVec2(text_x, kit_snap(pos.y + em * 1.25f)),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), chain);

    // The number (or the complaint) rides the body's right edge; a
    // known fiat worth sits under it in the caption voice.
    const char* value = ok ? balance : error_note;
    const float budget = pos.x + body_w - text_x - em * 4.0f;
    const std::string shown = kit_elide_middle(value, budget);
    const float w = ImGui::CalcTextSize(shown.c_str()).x;
    const ImU32 color = ok ? ImGui::GetColorU32(ImGuiCol_Text)
                           : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const bool two_lines = ok && fiat && *fiat;
    const float value_y
        = two_lines ? pos.y + em * 0.2f : pos.y + (row_h - em) * 0.5f;
    draw->AddText(ImVec2(kit_snap(pos.x + body_w - w), kit_snap(value_y)),
        color, shown.c_str());
    if (two_lines) {
        const float fw
            = ImGui::GetFont()
                  ->CalcTextSizeA(kit_caption_size(), FLT_MAX, 0.0f, fiat)
                  .x;
        draw->AddText(ImGui::GetFont(), kit_caption_size(),
            ImVec2(kit_snap(pos.x + body_w - fw), kit_snap(pos.y + em * 1.25f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), fiat);
    }

    if (with_menu) {
        // Three quiet discs, hand-drawn like every other glyph in the
        // kit — brighter under the pointer, with a soft ring behind.
        const ImVec2 c(pos.x + body_w + slot * 0.5f, pos.y + row_h * 0.5f);
        if (dots_hover) {
            ImVec4 ring = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ring.w = kit_is_dark() ? 0.08f : 0.06f;
            draw->AddCircleFilled(c, em * 0.75f, ImGui::GetColorU32(ring), 0);
        }
        const ImU32 dot = ImGui::GetColorU32(
            dots_hover ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        const float r = em * 0.07f;
        const float gap = em * 0.24f;
        for (int k = -1; k <= 1; ++k)
            draw->AddCircleFilled(
                ImVec2(kit_snap(c.x + gap * float(k)), kit_snap(c.y)), r, dot,
                0);
    }
    return ev;
}

}
