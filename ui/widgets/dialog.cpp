#include "ui/widgets/dialog.hpp"

#include <string>

#include "ui/widgets/design.hpp"
#include "ui/widgets/kit.hpp"

namespace izan::ui {

namespace {

    // One dialog width for every dialog: enough for a passphrase
    // field, narrow enough to read as an alert.
    float dialog_width()
    {
        return ImGui::GetFontSize() * design().dialog_width;
    }

    // The depth language painted onto the open dialog window: a faint
    // light falling from the top edge (dark themes). Depth beyond that
    // belongs to the shadow layer — a decorated border was tried and
    // retired: too much machinery for too little effect.
    void paint_window_decor()
    {
        const DesignLanguage& dl = design();
        if (!dl.dialog_wash || !kit_is_dark())
            return;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max(
            min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetWindowHeight());
        const float r = ImGui::GetStyle().PopupRounding;
        const float wash = ImGui::GetFontSize() * dl.wash_height;
        const ImU32 lit = IM_COL32(255, 255, 255, dl.wash_alpha);
        const ImU32 gone = IM_COL32(255, 255, 255, 0);
        draw->AddRectFilled(
            min, ImVec2(max.x, min.y + r), lit, r, ImDrawFlags_RoundCornersTop);
        draw->AddRectFilledMultiColor(ImVec2(min.x, min.y + r),
            ImVec2(max.x, min.y + wash), lit, lit, gone, gone);
    }

    void push_dialog_style()
    {
        const DesignLanguage& dl = design();
        const float em = ImGui::GetFontSize();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(em * dl.dialog_pad_x, em * dl.dialog_pad_y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(em * dl.dialog_row_gap, em * dl.dialog_row_gap));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, em * dl.dialog_radius);
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

        ImGui::PushFont(nullptr, ImGui::GetFontSize() * design().heading_scale);
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
                // Wrap at the dialog's fixed width, never the window's
                // own: an auto-resizing window that wraps to itself
                // oscillates between two widths and shimmers.
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + content);
                ImGui::TextUnformatted(subtitle);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::PopFont();
        }
        kit_vspace(0.35f);
    }

}

// An auto-resizing window has no measured size on its opening frame,
// so pivot-centering would land it off-axis for one frame and it
// would visibly jump into place. The opening frame is instead spent
// far off-screen taking measurements; the dialog first becomes
// visible already centered.
std::string g_dialog_appearing;

void kit_dialog_open(const char* id)
{
    ImGui::OpenPopup(id);
    g_dialog_appearing = id;
}

bool kit_dialog_begin(const char* id, bool* dismissed, bool escapable)
{
    push_dialog_style();
    if (g_dialog_appearing == id) {
        ImGui::SetNextWindowPos(ImVec2(-20000.0f, -20000.0f), ImGuiCond_Always);
        g_dialog_appearing.clear();
    } else {
        // Anchored to the viewport's center every frame — resizing the
        // host window must not leave the dialog drifting off-axis.
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    const bool open = ImGui::BeginPopupModal(id, nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoMove);
    if (!open) {
        pop_dialog_style();
        return false;
    }
    paint_window_decor();
    ImGui::Dummy(ImVec2(dialog_width(), 0.0f));
    if (escapable && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    const float size = ImGui::GetFontSize() * design().dialog_avatar;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - size) * 0.5f);
    kit_avatar(avatar_name, size);
    kit_vspace(0.25f);
    header_text(title, subtitle);
}

void kit_dialog_header_icon(
    const char* glyph, const char* title, const char* subtitle)
{
    const float glyph_size = ImGui::GetFontSize() * design().dialog_glyph;
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
    if (kit_subtle_button(cancel, w))
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
