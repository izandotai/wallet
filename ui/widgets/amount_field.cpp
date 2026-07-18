#include "ui/widgets/amount_field.hpp"

#include <cctype>
#include <string>
#include <unordered_map>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/text_field.hpp"

namespace izan::ui {

namespace {

    // Cross-frame state per amount field: the last accepted text (the
    // rollback point for a refused paste) and the refusal timestamp
    // that drives the border flash.
    struct AmountState {
        std::string shadow;
        float rejected_at = -1.0e9f;
    };

    AmountState& state_of(ImGuiID key)
    {
        static std::unordered_map<ImGuiID, AmountState> states;
        return states[key];
    }

    bool plain_decimal(const std::string& text)
    {
        bool digit = false, dot = false;
        for (const char c : text) {
            if (c >= '0' && c <= '9') {
                digit = true;
            } else if (c == '.') {
                if (dot)
                    return false;
                dot = true;
            } else {
                return false;
            }
        }
        return digit;
    }

    std::string trimmed_clipboard()
    {
        const char* clip = ImGui::GetClipboardText();
        if (!clip)
            return {};
        std::string text(clip);
        const auto first = text.find_first_not_of(" \t\r\n");
        const auto last = text.find_last_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        return text.substr(first, last - first + 1);
    }

    // The field normalizes as you type: only digits and one decimal
    // point survive, and a leading "." becomes "0." on the spot — the
    // number on screen is always the number that will be parsed. A
    // multi-character insertion is a paste, and a paste is all or
    // nothing: if the clipboard is not itself a plain number, the whole
    // edit is rolled back — filtering an address down to its digits
    // would leave a number-shaped lie.
    int amount_callback(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
            const ImWchar c = data->EventChar;
            return (c >= '0' && c <= '9') || c == '.' ? 0 : 1;
        }
        if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
            AmountState& st = *static_cast<AmountState*>(data->UserData);
            if (data->BufTextLen - int(st.shadow.size()) >= 2
                && !plain_decimal(trimmed_clipboard())) {
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, st.shadow.c_str());
                data->CursorPos = int(st.shadow.size());
                st.rejected_at = float(ImGui::GetTime());
                return 0;
            }
            if (data->BufTextLen > 0 && data->Buf[0] == '.')
                data->InsertChars(0, "0");
            bool seen_dot = false;
            for (int i = 0; i < data->BufTextLen;) {
                if (data->Buf[i] == '.') {
                    if (seen_dot) {
                        data->DeleteChars(i, 1);
                        continue;
                    }
                    seen_dot = true;
                }
                ++i;
            }
            st.shadow.assign(data->Buf, std::size_t(data->BufTextLen));
        }
        return 0;
    }

}

bool kit_amount_field(const char* id, char* buf, std::size_t size,
    const char* badge, bool* badge_clicked)
{
    const float em = ImGui::GetFontSize();
    const float w = ImGui::CalcItemWidth();
    const float big = kit_snap(em * 1.45f);
    const float h = big + em; // 0.5em of breath above and below

    ImGui::PushID(id);
    AmountState& st = state_of(ImGui::GetID("##in"));

    // The frame spans the full row; the input stops before the badge
    // so digits and caret never run under it.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    kit_field_frame(pos, ImVec2(w, h));
    float badge_zone = 0.0f;
    const float chevron = em * 0.55f;
    const float pad = em * 0.5f;
    float bw = 0.0f;
    if (badge && *badge) {
        bw = ImGui::CalcTextSize(badge).x + chevron + pad * 2.0f;
        badge_zone = bw + em * 0.7f;
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding, ImVec2(em * 0.55f, em * 0.5f));
    ImGui::PushFont(nullptr, big);
    ImGui::SetNextItemWidth(w - badge_zone);
    const bool submitted = ImGui::InputTextWithHint("##in", "0", buf, size,
        ImGuiInputTextFlags_EnterReturnsTrue
            | ImGuiInputTextFlags_CallbackCharFilter
            | ImGuiInputTextFlags_CallbackEdit,
        amount_callback, &st);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!ImGui::IsItemActive())
        st.shadow = buf; // pages may reset the buffer between frames

    const ImVec2 fmax(pos.x + w, pos.y + h);
    if (ImGui::GetTime() - st.rejected_at < 0.8) {
        ImGui::GetWindowDrawList()->AddRect(pos, fmax,
            ImGui::GetColorU32(kit_danger()), ImGui::GetStyle().FrameRounding,
            0, 2.0f);
    }

    if (badge && *badge) {
        // The badge: a quiet capsule with a chevron on the frame's
        // right edge.
        const float bh = em * 1.7f;
        const ImVec2 keep = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(
            ImVec2(fmax.x - bw - em * 0.35f, (pos.y + fmax.y - bh) * 0.5f));
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
        kit_cursor_restore(keep);
    }
    ImGui::PopID();
    return submitted;
}

}
