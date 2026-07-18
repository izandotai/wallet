#include "ui/widgets/dialog.hpp"

#include "ui/widgets/kit.hpp"

namespace izan::ui {

namespace {

    // One dialog width for every dialog: enough for a passphrase
    // field, narrow enough to read as an alert.
    float dialog_width()
    {
        return ImGui::GetFontSize() * 15.0f;
    }

    bool dark_theme()
    {
        const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        return 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z < 0.5f;
    }

    // The depth language painted onto the open dialog window: a faint
    // light falling from the top edge — a solid strip through the
    // rounded corners, then a smooth fade — and a crisp inner
    // highlight line right under the rim.
    void paint_window_decor()
    {
        if (!dark_theme())
            return; // light themes carry their own brightness
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max(
            min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetWindowHeight());
        const float r = ImGui::GetStyle().PopupRounding;
        const float wash = ImGui::GetFontSize() * 3.2f;
        const ImU32 lit = IM_COL32(255, 255, 255, 10);
        const ImU32 gone = IM_COL32(255, 255, 255, 0);

        draw->AddRectFilled(
            min, ImVec2(max.x, min.y + r), lit, r, ImDrawFlags_RoundCornersTop);
        draw->AddRectFilledMultiColor(ImVec2(min.x, min.y + r),
            ImVec2(max.x, min.y + wash), lit, lit, gone, gone);
        draw->AddLine(ImVec2(min.x + r, min.y + 1.0f),
            ImVec2(max.x - r, min.y + 1.0f), IM_COL32(255, 255, 255, 26));
    }

    void push_dialog_style()
    {
        const float em = ImGui::GetFontSize();
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding, ImVec2(em * 1.1f, em * 0.95f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing, ImVec2(em * 0.5f, em * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, em * 0.45f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    }

    void pop_dialog_style()
    {
        ImGui::PopStyleVar(4);
    }

    void header_text(const char* title, const char* subtitle)
    {
        const float width = ImGui::GetWindowWidth();
        auto centered = [&](float w) {
            const float x = (width - w) * 0.5f;
            ImGui::SetCursorPosX(x > 0.0f ? x : 0.0f);
        };

        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.15f);
        centered(ImGui::CalcTextSize(title).x);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();

        if (subtitle && *subtitle) {
            ImGui::PushFont(nullptr, kit_caption_size());
            const float content = dialog_width();
            const float w = ImGui::CalcTextSize(subtitle).x;
            if (w <= content) {
                centered(w);
                ImGui::TextDisabled("%s", subtitle);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", subtitle);
                ImGui::PopStyleColor();
            }
            ImGui::PopFont();
        }
        kit_vspace(0.35f);
    }

}

void kit_dialog_open(const char* id)
{
    ImGui::OpenPopup(id);
}

bool kit_dialog_begin(const char* id, bool* dismissed)
{
    push_dialog_style();
    const bool open = ImGui::BeginPopupModal(id, nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoMove);
    if (!open) {
        pop_dialog_style();
        return false;
    }
    paint_window_decor();
    ImGui::Dummy(ImVec2(dialog_width(), 0.0f));
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (dismissed)
            *dismissed = true;
        ImGui::CloseCurrentPopup();
    }
    return true;
}

void kit_dialog_end()
{
    ImGui::EndPopup();
    pop_dialog_style();
}

void kit_dialog_close()
{
    ImGui::CloseCurrentPopup();
}

void kit_dialog_header_avatar(
    const char* avatar_name, const char* title, const char* subtitle)
{
    const float size = ImGui::GetFontSize() * 2.4f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - size) * 0.5f);
    kit_avatar(avatar_name, size);
    kit_vspace(0.25f);
    header_text(title, subtitle);
}

void kit_dialog_header_icon(
    const char* glyph, const char* title, const char* subtitle)
{
    const float glyph_size = ImGui::GetFontSize() * 2.0f;
    ImGui::PushFont(nullptr, glyph_size);
    ImGui::SetCursorPosX(
        (ImGui::GetWindowWidth() - ImGui::CalcTextSize(glyph).x) * 0.5f);
    ImGui::TextUnformatted(glyph);
    ImGui::PopFont();
    kit_vspace(0.25f);
    header_text(title, subtitle);
}

void kit_dialog_field_width()
{
    ImGui::SetNextItemWidth(dialog_width());
}

int kit_dialog_buttons(const char* cancel, const char* confirm,
    bool confirm_enabled, bool destructive)
{
    kit_vspace(0.35f);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float w = (dialog_width() - gap) * 0.5f;
    int choice = 0;
    if (ImGui::Button(cancel, ImVec2(w, 0.0f)))
        choice = 1;
    ImGui::SameLine();
    ImGui::BeginDisabled(!confirm_enabled);
    const bool ok = destructive ? kit_danger_button(confirm, w)
                                : kit_primary_button(confirm, w);
    ImGui::EndDisabled();
    if (ok)
        choice = 2;
    return choice;
}

}
