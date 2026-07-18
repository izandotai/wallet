#include "ui/widgets/menu.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/widgets/avatar.hpp"
#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

namespace {

    // The shell's dropdown numbers, mirrored exactly — one rhythm for
    // every menu in the app (chrome_widgets pushes the same values on
    // the menu bar's own popups).
    void push_menu_style()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 11.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 12.0f));
        ImVec4 popup_bg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        popup_bg.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, popup_bg);
    }

    void pop_menu_style()
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

}

bool kit_menu_begin(const char* id)
{
    push_menu_style();
    const bool open = ImGui::BeginPopup(id);
    if (!open) {
        pop_menu_style();
        return false;
    }
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    return true;
}

void kit_menu_end()
{
    ImGui::PopItemFlag();
    ImGui::EndPopup();
    pop_menu_style();
}

bool kit_menu_item(
    const char* label, const char* trailing, bool selected, bool enabled)
{
    return ImGui::MenuItem(label, trailing, selected, enabled);
}

float kit_menu_row_width(const char* label, const char* trailing)
{
    const float em = ImGui::GetFontSize();
    float w = em * 0.95f + em * 0.55f + ImGui::CalcTextSize(label).x;
    if (trailing && *trailing)
        w += em * 1.6f
            + ImGui::GetFont()
                  ->CalcTextSizeA(kit_caption_size(), FLT_MAX, 0.0f, trailing)
                  .x;
    return w;
}

bool kit_menu_item_icon(const char* swatch_name, const char* label,
    const char* trailing, bool selected, float width)
{
    const float em = ImGui::GetFontSize();
    const float sw = em * 0.95f;
    const float row_h = em * 1.7f;
    if (width <= 0.0f)
        width = kit_menu_row_width(label, trailing);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked
        = ImGui::Selectable("##row", selected, 0, ImVec2(width, row_h));
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // The style-editor grammar: a flat color square speaks for the
    // thing, the label names it, the trailing note keeps to the right
    // edge every row shares.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 sp(kit_snap(pos.x), kit_snap(pos.y + (row_h - sw) * 0.5f));
    draw->AddRectFilled(sp, ImVec2(sp.x + sw, sp.y + sw),
        ImGui::GetColorU32(kit_identity_color(swatch_name)), sw * 0.22f);
    draw->AddRect(
        sp, ImVec2(sp.x + sw, sp.y + sw), IM_COL32(0, 0, 0, 40), sw * 0.22f);
    draw->AddText(ImVec2(kit_snap(pos.x + sw + em * 0.55f),
                      kit_snap(pos.y + (row_h - em) * 0.5f)),
        ImGui::GetColorU32(ImGuiCol_Text), label);
    if (trailing && *trailing) {
        const float trailing_w
            = ImGui::GetFont()
                  ->CalcTextSizeA(kit_caption_size(), FLT_MAX, 0.0f, trailing)
                  .x;
        draw->AddText(ImGui::GetFont(), kit_caption_size(),
            ImVec2(kit_snap(pos.x + width - trailing_w),
                kit_snap(pos.y + (row_h - kit_caption_size()) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), trailing);
    }
    return clicked;
}

}
