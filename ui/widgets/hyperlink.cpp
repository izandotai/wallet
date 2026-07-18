#include "ui/widgets/hyperlink.hpp"

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>

#include <imgui.h>

#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

void kit_hyperlink(const char* id, const char* label, const char* url)
{
    const std::string shown
        = kit_elide_middle(label, ImGui::GetContentRegionAvail().x);

    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text, kit_accent());
    ImGui::TextUnformatted(shown.c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y),
            ImVec2(max.x, max.y), ImGui::GetColorU32(kit_accent()));
        ImGui::SetTooltip("%s", url);
    }
    if (ImGui::IsItemClicked())
        ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        ImGui::SetClipboardText(url);
    ImGui::PopID();
}

}
