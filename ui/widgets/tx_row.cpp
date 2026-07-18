#include "ui/widgets/tx_row.hpp"

#include <cfloat>
#include <string>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/tooltip.hpp"

namespace izan::ui {

bool kit_tx_row(const char* id, bool incoming, const char* counterparty,
    const char* note, const char* amount, bool failed, const char* hint,
    const char* note_hint)
{
    const float em = ImGui::GetFontSize();
    const float row_h = em * 2.3f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(row_w, row_h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered) {
        const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        draw->AddRectFilled(ImVec2(pos.x - em * 0.3f, pos.y),
            ImVec2(pos.x + row_w + em * 0.3f, pos.y + row_h),
            ImGui::GetColorU32(
                kit_blend(bg, text, kit_is_dark() ? 0.06f : 0.045f)),
            em * design().selection_radius);
    }

    // The direction disc: accent for money arriving, quiet for money
    // leaving, danger-toned for a movement the chain rejected.
    const float disc = em * 1.5f;
    const ImVec2 c(pos.x + disc * 0.5f, pos.y + row_h * 0.5f);
    ImVec4 tone = failed ? kit_danger()
        : incoming       ? kit_accent()
                         : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    tone.w = 0.22f;
    draw->AddCircleFilled(c, disc * 0.5f, ImGui::GetColorU32(tone), 0);
    tone.w = 1.0f;
    const ImU32 stroke = ImGui::GetColorU32(tone);
    const float a = disc * 0.21f;
    const float t = disc * 0.075f;
    // A plain arrow: shaft plus head, pointing down-in or up-out.
    const float dir = incoming ? 1.0f : -1.0f;
    draw->AddLine(
        ImVec2(c.x, c.y - a * dir), ImVec2(c.x, c.y + a * dir), stroke, t);
    draw->AddLine(ImVec2(c.x - a * 0.7f, c.y + a * dir * 0.25f),
        ImVec2(c.x, c.y + a * dir), stroke, t);
    draw->AddLine(ImVec2(c.x + a * 0.7f, c.y + a * dir * 0.25f),
        ImVec2(c.x, c.y + a * dir), stroke, t);

    const float text_x = kit_snap(pos.x + disc + em * 0.5f);
    const float amount_w = ImGui::CalcTextSize(amount).x;
    const float budget = pos.x + row_w - text_x - amount_w - em * 0.8f;
    const std::string who = kit_elide_middle(counterparty, budget);
    draw->AddText(ImVec2(text_x, kit_snap(pos.y + em * 0.2f)),
        ImGui::GetColorU32(ImGuiCol_Text), who.c_str());
    const float cap = kit_caption_size();
    const ImVec2 note_pos(text_x, kit_snap(pos.y + em * 1.25f));
    draw->AddText(ImGui::GetFont(), cap, note_pos,
        ImGui::GetColorU32(ImGuiCol_TextDisabled), note);
    if (hovered && (hint || note_hint)) {
        // Measured at the size it was drawn — the rule that keeps the
        // hot zone honest.
        const float note_w
            = ImGui::GetFont()->CalcTextSizeA(cap, FLT_MAX, 0.0f, note).x;
        const ImVec2 m = ImGui::GetMousePos();
        const bool on_note = note_hint && m.x >= note_pos.x
            && m.x <= note_pos.x + note_w && m.y >= note_pos.y
            && m.y <= note_pos.y + cap;
        if (on_note)
            kit_tooltip(note_hint);
        else if (hint)
            kit_tooltip(hint);
    }
    draw->AddText(ImVec2(kit_snap(pos.x + row_w - amount_w),
                      kit_snap(pos.y + (row_h - em) * 0.5f)),
        failed ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
               : ImGui::GetColorU32(ImGuiCol_Text),
        amount);
    return clicked;
}

}
