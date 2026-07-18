#include "ui/widgets/menu.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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

}
