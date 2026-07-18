#include "ui/widgets/tooltip.hpp"

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
    if (!ImGui::BeginTooltip())
        return;
    // Long text wraps into a paragraph instead of one absurd ribbon,
    // and a truly huge payload caps its height behind a scrollbar so
    // the tooltip never swallows the screen.
    const float em = ImGui::GetFontSize();
    const float wrap_w = em * 26.0f;
    const ImVec2 need = ImGui::CalcTextSize(text, nullptr, false, wrap_w);
    if (need.x <= wrap_w && need.y <= ImGui::GetTextLineHeight() * 1.5f) {
        ImGui::TextUnformatted(text);
    } else {
        const float max_h = em * 11.0f;
        if (need.y > max_h) {
            ImGui::BeginChild("##tooltip-scroll",
                ImVec2(wrap_w + ImGui::GetStyle().ScrollbarSize + em * 0.3f,
                    max_h));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        } else {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
        }
    }
    if (hint && *hint)
        kit_caption(hint);
    ImGui::EndTooltip();
}

}
