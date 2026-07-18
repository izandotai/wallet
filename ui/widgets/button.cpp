#include "ui/widgets/button.hpp"

#include "ui/widgets/design.hpp"

namespace izan::ui {

namespace {

    bool filled_button(const char* label, float width, const ImVec4& fill)
    {
        const ImVec4 hover = kit_blend(fill, ImVec4(1, 1, 1, fill.w), 0.12f);
        const ImVec4 active = kit_blend(fill, ImVec4(0, 0, 0, fill.w), 0.12f);
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

bool kit_subtle_button(const char* label, float width)
{
    return ImGui::Button(label, ImVec2(width, 0.0f));
}

bool kit_link_button(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kit_accent());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return clicked;
}

}
