#include "ui/widgets/amount_field.hpp"

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/text_field.hpp"

namespace izan::ui {

bool kit_amount_field(const char* id, char* buf, std::size_t size,
    const char* badge, bool* badge_clicked)
{
    const float em = ImGui::GetFontSize();
    const float w = ImGui::CalcItemWidth();
    const float big = kit_snap(em * 1.45f);

    ImGui::PushID(id);
    kit_field_style_push();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding, ImVec2(em * 0.55f, em * 0.5f));
    ImGui::PushFont(nullptr, big);
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetNextItemWidth(w);
    const bool submitted = ImGui::InputTextWithHint("##in", "0", buf, size,
        ImGuiInputTextFlags_CharsDecimal
            | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopFont();
    ImGui::PopStyleVar();
    kit_field_style_pop();
    const ImVec2 fmin = ImGui::GetItemRectMin();
    const ImVec2 fmax = ImGui::GetItemRectMax();

    if (badge && *badge) {
        // The badge: a quiet capsule with a chevron, living inside the
        // field's right edge. Opaque over the field so long digit runs
        // pass under it, not through it.
        const float chevron = em * 0.55f;
        const float pad = em * 0.5f;
        const float bw = ImGui::CalcTextSize(badge).x + chevron + pad * 2.0f;
        const float bh = em * 1.7f;
        const ImVec2 keep = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(
            ImVec2(fmax.x - bw - em * 0.35f, (fmin.y + fmax.y - bh) * 0.5f));
        ImGui::InvisibleButton("##badge", ImVec2(bw, bh));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (badge_clicked && ImGui::IsItemClicked())
            *badge_clicked = true;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImVec2 bmax = ImGui::GetItemRectMax();
        const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const float lift = kit_is_dark() ? 0.055f : 0.04f;
        draw->AddRectFilled(bmin, bmax,
            ImGui::GetColorU32(kit_blend(bg, text, lift)), bh * 0.5f);
        draw->AddRectFilled(bmin, bmax,
            ImGui::GetColorU32(kit_blend(bg, text, hovered ? 0.16f : 0.09f)),
            bh * 0.5f);
        draw->AddText(ImVec2(kit_snap(bmin.x + pad),
                          kit_snap((bmin.y + bmax.y - em) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_Text), badge);
        const float cx = bmax.x - pad - chevron * 0.5f;
        const float cy = (bmin.y + bmax.y) * 0.5f - chevron * 0.1f;
        draw->AddTriangleFilled(ImVec2(cx - chevron * 0.4f, cy),
            ImVec2(cx + chevron * 0.4f, cy), ImVec2(cx, cy + chevron * 0.42f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::SetCursorScreenPos(keep);
    }
    ImGui::PopID();
    return submitted;
}

}
