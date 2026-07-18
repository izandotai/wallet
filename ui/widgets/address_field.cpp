#include "ui/widgets/address_field.hpp"

#include <cctype>
#include <cstring>
#include <string>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/text_field.hpp"

namespace izan::ui {

namespace {

    // Clipboard text arrives with the neighborhood it was copied from;
    // an address wants none of it.
    void paste_into(char* buf, std::size_t size)
    {
        const char* clip = ImGui::GetClipboardText();
        if (!clip)
            return;
        std::string text(clip);
        const auto first = text.find_first_not_of(" \t\r\n");
        const auto last = text.find_last_not_of(" \t\r\n");
        if (first == std::string::npos)
            return;
        text = text.substr(first, last - first + 1);
        std::strncpy(buf, text.c_str(), size - 1);
        buf[size - 1] = '\0';
    }

    void draw_paste_glyph(ImDrawList* draw, ImVec2 pos, float d, ImU32 color)
    {
        // A clipboard: the board, and the clip riding its top edge.
        const float r = d * 0.12f;
        draw->AddRect(ImVec2(pos.x + d * 0.2f, pos.y + d * 0.16f),
            ImVec2(pos.x + d * 0.8f, pos.y + d * 0.92f), color, r, 0,
            d * 0.075f);
        draw->AddRectFilled(ImVec2(pos.x + d * 0.36f, pos.y + d * 0.06f),
            ImVec2(pos.x + d * 0.64f, pos.y + d * 0.24f), color, r * 0.7f);
    }

    void draw_clear_glyph(ImDrawList* draw, ImVec2 pos, float d, ImU32 color)
    {
        const ImVec2 c(pos.x + d * 0.5f, pos.y + d * 0.5f);
        draw->AddCircleFilled(c, d * 0.42f, color, 0);
        const ImU32 x = ImGui::GetColorU32(ImGuiCol_WindowBg);
        const float a = d * 0.16f;
        const float t = d * 0.085f;
        draw->AddLine(ImVec2(c.x - a, c.y - a), ImVec2(c.x + a, c.y + a), x, t);
        draw->AddLine(ImVec2(c.x - a, c.y + a), ImVec2(c.x + a, c.y - a), x, t);
    }

}

bool kit_address_field(const char* id, const char* hint, char* buf,
    std::size_t size, const char* paste_label, const char* copy_label,
    const char* clear_label)
{
    const float em = ImGui::GetFontSize();
    const float w = ImGui::CalcItemWidth();

    ImGui::PushID(id);
    // Let the trailing glyph win the hover contest over the input —
    // without this the text field swallows every click on the button.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetNextItemWidth(w);
    bool submitted = kit_text_field("##text", hint, buf, size);
    const ImVec2 fmin = ImGui::GetItemRectMin();
    const ImVec2 fmax = ImGui::GetItemRectMax();
    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    // The trailing glyph: paste into an empty field, clear a full one.
    const bool empty = buf[0] == '\0';
    const float d = em * 1.1f;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(
        ImVec2(fmax.x - d - em * 0.3f, (fmin.y + fmax.y - d) * 0.5f));
    ImGui::InvisibleButton("##glyph", ImVec2(d, d));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked()) {
        if (empty)
            paste_into(buf, size);
        else
            buf[0] = '\0';
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 tone
        = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const ImVec2 gpos = ImGui::GetItemRectMin();
    if (empty)
        draw_paste_glyph(draw, gpos, d, tone);
    else
        draw_clear_glyph(draw, gpos, d, tone);
    ImGui::SetCursorScreenPos(keep);

    if (ImGui::BeginPopup("##menu")) {
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        if (ImGui::MenuItem(paste_label))
            paste_into(buf, size);
        if (ImGui::MenuItem(copy_label, nullptr, false, !empty))
            ImGui::SetClipboardText(buf);
        if (ImGui::MenuItem(clear_label, nullptr, false, !empty))
            buf[0] = '\0';
        ImGui::PopItemFlag();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return submitted;
}

}
