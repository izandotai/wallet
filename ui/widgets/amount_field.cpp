#include "ui/widgets/amount_field.hpp"

#include <imgui.h>

#include "ui/widgets/design.hpp"

namespace izan::ui {

bool kit_amount_field(
    const char* id, char* buf, std::size_t size, const char* unit)
{
    const float em = ImGui::GetFontSize();
    const float max_w = ImGui::GetContentRegionAvail().x - em * 0.5f;
    const char* shown = buf[0] ? buf : "0";
    ImFont* font = ImGui::GetFont();

    // Start large and step the type down until digits, caret room and
    // unit all fit the row.
    float big = kit_snap(em * 2.1f);
    float text_w = 0.0f, unit_w = 0.0f, unit_size = 0.0f, gap = 0.0f;
    for (;; big = kit_snap(big * 0.88f)) {
        text_w = font->CalcTextSizeA(big, FLT_MAX, 0.0f, shown).x;
        unit_size = kit_snap(big * 0.5f);
        if (unit && *unit) {
            gap = big * 0.2f;
            unit_w = font->CalcTextSizeA(unit_size, FLT_MAX, 0.0f, unit).x;
        }
        if (text_w + em * 0.8f + gap + unit_w <= max_w || big <= em)
            break;
    }

    const float field_w = text_w + em * 0.8f;
    const float slack = max_w - field_w - gap - unit_w;
    if (slack > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack * 0.5f);

    ImGui::PushFont(nullptr, big);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(field_w);
    const bool submitted = ImGui::InputTextWithHint(id, "0", buf, size,
        ImGuiInputTextFlags_CharsDecimal
            | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // The unit shares the digits' baseline, half their size and muted
    // — the number speaks, the unit whispers.
    if (unit && *unit) {
        const ImVec2 fmin = ImGui::GetItemRectMin();
        const ImVec2 fmax = ImGui::GetItemRectMax();
        const float pad = (fmax.y - fmin.y - big) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(font, unit_size,
            ImVec2(kit_snap(fmax.x + gap - em * 0.4f),
                kit_snap(fmin.y + pad + big - unit_size * 1.12f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), unit);
    }
    return submitted;
}

}
