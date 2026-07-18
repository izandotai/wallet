#include "ui/widgets/identity.hpp"

#include <string>

#include <imgui.h>

#include "ui/widgets/avatar.hpp"
#include "ui/widgets/copy_text.hpp"
#include "ui/widgets/design.hpp"
#include "ui/widgets/label.hpp"

namespace izan::ui {

namespace {

    void centered(float item_w, float avail, float x0)
    {
        const float slack = avail - item_w;
        ImGui::SetCursorPosX(x0 + (slack > 0.0f ? slack * 0.5f : 0.0f));
    }

}

void kit_identity(const char* title, const char* subtitle, const char* hero,
    float avatar_em, const char* copy_hint, const char* copied_label)
{
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float x0 = ImGui::GetCursorPosX();

    const float av = em * avatar_em;
    centered(av, avail, x0);
    kit_avatar(title, av);
    kit_vspace(0.3f);

    ImGui::PushFont(nullptr, kit_heading_size());
    centered(ImGui::CalcTextSize(title).x, avail, x0);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();

    if (subtitle && *subtitle) {
        if (copy_hint && copied_label) {
            kit_copy_text_centered(
                "##identity-sub", subtitle, copy_hint, copied_label);
        } else {
            ImGui::PushFont(nullptr, kit_caption_size());
            const std::string shown
                = kit_elide_middle(subtitle, avail - em, kit_caption_size());
            centered(ImGui::CalcTextSize(shown.c_str()).x, avail, x0);
            ImGui::TextDisabled("%s", shown.c_str());
            ImGui::PopFont();
        }
    }

    if (hero && *hero) {
        kit_vspace(0.35f);
        ImGui::PushFont(nullptr, kit_snap(em * 1.6f));
        centered(ImGui::CalcTextSize(hero).x, avail, x0);
        ImGui::TextUnformatted(hero);
        ImGui::PopFont();
    }
}

}
