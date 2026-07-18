#include "ui/widgets/tooltip.hpp"

#include <string>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

void kit_tooltip(const char* text)
{
    kit_tooltip_lines(text, nullptr);
}

void kit_tooltip_lines(const char* text, const char* hint)
{
    // Dressed like the rest of the kit: real padding, soft corners.
    // Long text wraps into a measured paragraph; a truly enormous
    // payload is trimmed with an honest ellipsis — a tooltip is a
    // glance, not a document viewer.
    const float em = ImGui::GetFontSize();
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding, ImVec2(em * 0.65f, em * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, em * 0.35f);
    if (!ImGui::BeginTooltip()) {
        ImGui::PopStyleVar(2);
        return;
    }
    const float wrap_w = em * 24.0f;
    const float max_h = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
    std::string shown(text);
    ImVec2 need = ImGui::CalcTextSize(shown.c_str(), nullptr, false, wrap_w);
    while (need.y > max_h && shown.size() > 16) {
        shown.resize(shown.size() * 9 / 10);
        need = ImGui::CalcTextSize(
            (shown + " …").c_str(), nullptr, false, wrap_w);
    }
    if (shown.size() != std::string(text).size())
        shown += " …";
    if (need.x > wrap_w || need.y > ImGui::GetTextLineHeight() * 1.5f) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
        ImGui::TextUnformatted(shown.c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextUnformatted(shown.c_str());
    }
    if (hint && *hint)
        kit_caption(hint);
    ImGui::EndTooltip();
    ImGui::PopStyleVar(2);
}

}
