#include "ui/widgets/asset_row.hpp"

#include <string>

#include <imgui.h>

#include "ui/widgets/avatar.hpp"
#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

void kit_asset_row(const char* id, const char* symbol, const char* chain,
    const char* balance, bool ok, const char* error_note, const char* fiat)
{
    const float em = ImGui::GetFontSize();
    const float row_h = em * 2.3f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    ImGui::Dummy(ImVec2(row_w, row_h));
    ImGui::PopID();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float avatar = em * 1.6f;
    kit_avatar_at(
        ImVec2(pos.x, pos.y + (row_h - avatar) * 0.5f), symbol, avatar);

    const float text_x = kit_snap(pos.x + avatar + em * 0.5f);
    draw->AddText(ImVec2(text_x, kit_snap(pos.y + em * 0.2f)),
        ImGui::GetColorU32(ImGuiCol_Text), symbol);
    draw->AddText(ImGui::GetFont(), kit_caption_size(),
        ImVec2(text_x, kit_snap(pos.y + em * 1.25f)),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), chain);

    // The number (or the complaint) rides the right edge; a known
    // fiat worth sits under it in the caption voice.
    const char* value = ok ? balance : error_note;
    const float budget = pos.x + row_w - text_x - em * 4.0f;
    const std::string shown = kit_elide_middle(value, budget);
    const float w = ImGui::CalcTextSize(shown.c_str()).x;
    const ImU32 color = ok ? ImGui::GetColorU32(ImGuiCol_Text)
                           : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const bool two_lines = ok && fiat && *fiat;
    const float value_y
        = two_lines ? pos.y + em * 0.2f : pos.y + (row_h - em) * 0.5f;
    draw->AddText(ImVec2(kit_snap(pos.x + row_w - w), kit_snap(value_y)), color,
        shown.c_str());
    if (two_lines) {
        const float fw
            = ImGui::GetFont()
                  ->CalcTextSizeA(kit_caption_size(), FLT_MAX, 0.0f, fiat)
                  .x;
        draw->AddText(ImGui::GetFont(), kit_caption_size(),
            ImVec2(kit_snap(pos.x + row_w - fw), kit_snap(pos.y + em * 1.25f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), fiat);
    }
}

}
