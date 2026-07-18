#include "ui/widgets/address_field.hpp"

#include <cstring>
#include <string>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/menu.hpp"
#include "ui/widgets/text_field.hpp"

namespace izan::ui {

namespace {

    // Clipboard text arrives with the neighborhood it was copied from;
    // an address wants none of it. Returns false when the validator
    // turns the candidate away — the buffer stays untouched.
    bool paste_into(char* buf, std::size_t size,
        const std::function<bool(const char*)>& validate)
    {
        const char* clip = ImGui::GetClipboardText();
        if (!clip)
            return false;
        std::string text(clip);
        const auto first = text.find_first_not_of(" \t\r\n");
        const auto last = text.find_last_not_of(" \t\r\n");
        if (first == std::string::npos)
            return false;
        text = text.substr(first, last - first + 1);
        if (validate && !validate(text.c_str()))
            return false;
        std::strncpy(buf, text.c_str(), size - 1);
        buf[size - 1] = '\0';
        return true;
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
    const char* clear_label, const std::function<bool(const char*)>& validate)
{
    const float em = ImGui::GetFontSize();
    const float w = ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();
    const float d = em * 1.1f;

    ImGui::PushID(id);

    // The frame spans the full row; the input stops before the glyph
    // zone so text and caret never run under the button.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    kit_field_frame(pos, ImVec2(w, h));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(w - d - em * 0.55f);
    const bool submitted = ImGui::InputTextWithHint(
        "##text", hint, buf, size, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    const ImVec2 fmax(pos.x + w, pos.y + h);
    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    // A refused paste answers with a moment of danger-colored border —
    // a button that silently does nothing reads as a broken button.
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID reject_key = ImGui::GetID("##rejected-at");
    auto try_paste = [&] {
        if (!paste_into(buf, size, validate))
            store->SetFloat(reject_key, float(ImGui::GetTime()));
    };
    const float rejected_at = store->GetFloat(reject_key, -1.0e9f);
    if (ImGui::GetTime() - rejected_at < 0.8) {
        ImGui::GetWindowDrawList()->AddRect(pos, fmax,
            ImGui::GetColorU32(kit_danger()), ImGui::GetStyle().FrameRounding,
            0, 2.0f);
    }

    // The trailing glyph: paste into an empty field, clear a full one.
    const bool empty = buf[0] == '\0';
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(
        ImVec2(fmax.x - d - em * 0.3f, (pos.y + fmax.y - d) * 0.5f));
    ImGui::InvisibleButton("##glyph", ImVec2(d, d));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked()) {
        if (empty)
            try_paste();
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
    kit_cursor_restore(keep);

    if (kit_menu_begin("##menu")) {
        if (kit_menu_item(paste_label))
            try_paste();
        if (kit_menu_item(copy_label, nullptr, false, !empty))
            ImGui::SetClipboardText(buf);
        if (kit_menu_item(clear_label, nullptr, false, !empty))
            buf[0] = '\0';
        kit_menu_end();
    }
    ImGui::PopID();
    return submitted;
}

}
