#include "ui/widgets/copy_text.hpp"

#include <cstring>
#include <string>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/tooltip.hpp"

namespace izan::ui {

namespace {

    constexpr double kFeedbackSeconds = 1.6;

    // Middle elision to a pixel budget: keep both ends — the parts a
    // person actually compares — and give up the middle. Addresses are
    // ASCII, so byte slicing is character-safe.
    std::string elide_to_fit(const char* full, float budget)
    {
        if (ImGui::CalcTextSize(full).x <= budget)
            return full;
        const std::size_t len = std::strlen(full);
        const std::size_t tail = len < 6 ? len : 6;
        for (std::size_t head = len; head > 4; --head) {
            std::string out(full, head);
            out += "…";
            out.append(full + len - tail, tail);
            if (ImGui::CalcTextSize(out.c_str()).x <= budget)
                return out;
        }
        std::string out(full, 4);
        out += "…";
        out.append(full + len - tail, tail);
        return out;
    }

    void copy_text_impl(const char* id, const char* full, const char* hint,
        const char* copied_label, bool right_align)
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID key = ImGui::GetID(id);
        const bool fresh
            = ImGui::GetTime() - double(storage->GetFloat(key, -1000.0f))
            < kFeedbackSeconds;

        // Never glued to the previous item: a breath of space stays
        // even when the row runs tight, and the text shrinks to fit.
        const float gap = ImGui::GetFontSize() * 0.6f;
        const float avail = ImGui::GetContentRegionAvail().x - gap;
        const std::string text
            = elide_to_fit(fresh ? copied_label : full, avail);
        if (right_align) {
            const float min_x = ImGui::GetCursorPosX() + gap;
            const float edge
                = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            const float x = edge - ImGui::CalcTextSize(text.c_str()).x;
            ImGui::SetCursorPosX(x > min_x ? x : min_x);
        }
        if (fresh) {
            ImGui::TextColored(kit_accent(), "%s", text.c_str());
            return;
        }
        ImGui::TextUnformatted(text.c_str());
        if (ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(full);
            storage->SetFloat(key, float(ImGui::GetTime()));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            kit_tooltip_lines(full, hint);
        }
    }

}

void kit_copy_text(const char* id, const char* full, const char* hint,
    const char* copied_label)
{
    copy_text_impl(id, full, hint, copied_label, false);
}

void kit_copy_text_right(const char* id, const char* full, const char* hint,
    const char* copied_label)
{
    copy_text_impl(id, full, hint, copied_label, true);
}

}
