#include "ui/widgets/text_field.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

#include "ui/widgets/design.hpp"

namespace izan::ui {

// Every field in the app wears the same clothes: a quiet lift off
// the window, a hairline border, room to breathe.
void kit_field_style_push()
{
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg, kit_blend(bg, text, kit_is_dark() ? 0.055f : 0.04f));
    ImGui::PushStyleColor(
        ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
}

void kit_field_style_pop()
{
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void kit_field_frame(const ImVec2& pos, const ImVec2& size)
{
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    const float r = ImGui::GetStyle().FrameRounding;
    draw->AddRectFilled(pos, max,
        ImGui::GetColorU32(kit_blend(bg, text, kit_is_dark() ? 0.055f : 0.04f)),
        r);
    draw->AddRect(pos, max,
        ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Separator)), r);
}

bool kit_text_field(
    const char* id, const char* hint, char* buf, std::size_t size)
{
    kit_field_style_push();
    const bool submitted = ImGui::InputTextWithHint(
        id, hint, buf, size, ImGuiInputTextFlags_EnterReturnsTrue);
    kit_field_style_pop();
    return submitted;
}

bool secret_field(const char* label, std::array<char, 256>& buf,
    bool& secret_focus, const char* hint)
{
    constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_Password
        | ImGuiInputTextFlags_AutoSelectAll
        | ImGuiInputTextFlags_EnterReturnsTrue;
    kit_field_style_push();
    const bool submitted = hint
        ? ImGui::InputTextWithHint(label, hint, buf.data(), buf.size(), kFlags)
        : ImGui::InputText(label, buf.data(), buf.size(), kFlags);
    kit_field_style_pop();
    secret_focus |= ImGui::IsItemActive();
    return submitted;
}

bool kit_paste_box(const char* id, const char* hint, char* buf,
    std::size_t size, float rows, bool& secret_focus)
{
    const float em = ImGui::GetFontSize();
    kit_field_style_push();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding, ImVec2(em * 0.55f, em * 0.45f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool changed = ImGui::InputTextMultiline(id, buf, size,
        ImVec2(-1.0f, ImGui::GetTextLineHeight() * rows + em * 0.9f));
    const bool active = ImGui::IsItemActive();
    ImGui::PopStyleVar();
    kit_field_style_pop();
    secret_focus |= active;

    // The hint, painted while the box is empty — multiline inputs have
    // no built-in one.
    if (hint && *hint && buf[0] == '\0' && !active)
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(pos.x + em * 0.55f, pos.y + em * 0.45f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
    return changed;
}

void kit_focus_here()
{
    ImGui::SetKeyboardFocusHere();
    ImGui::SetNavCursorVisible(false);
}

secure::SecureBytes take_secret(std::array<char, 256>& buf)
{
    const std::size_t len = strnlen(buf.data(), buf.size());
    secure::SecureBytes out(len);
    if (len)
        std::memcpy(out.data(), buf.data(), len);
    sodium_memzero(buf.data(), buf.size());
    return out;
}

}
