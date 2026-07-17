// Shell acceptance harness: boots the full izan shell — borderless
// chrome, 15 themes, CJK/emoji fonts, snap layouts, dock guardian —
// with placeholder panes. This is what a human eyeballs after any
// shell or dependency change; the wallet app proper has its own entry
// point.
#include "ui/shell/app.hpp"
#include "ui/shell/chrome_state.hpp"
#include "ui/shell/chrome_widgets.hpp"
#include "ui/shell/constants.hpp"
#include "ui/shell/ime.hpp"
#include "ui/shell/theme.hpp"
#include "ui/shell/ui_layout.hpp"

#include <imgui.h>

#include <GLFW/glfw3.h>

namespace {

void draw_placeholder_pane(const char* name, const char* line)
{
    ImGui::Begin(name);
    ImGui::TextUnformatted(line);
    ImGui::TextDisabled("中文渲染 ✓  emoji 🎨🔑💰  symbols ★◆①");
    ImGui::End();
}

}

int main()
{
    izan::ui::GlfwApp app;
    izan::ui::AppOptions options;
    options.title = "izan";
    options.width = 1600;
    options.height = 900;
    if (!app.init(options))
        return 1;

    izan::ui::ChromeState chrome;
    bool show_demo = false;

    app.set_render_callback([&] {
        app.begin_frame();

        izan::ui::draw_main_window_frame(chrome);
        izan::ui::draw_custom_title_bar(
            app.window(), chrome, "izan", "shell harness");
        // Deferred: applying a theme inside the menu callback gets its
        // WindowBg/PopupBg clobbered by the menu bar's PopStyleColor.
        int pending_theme = -1;
        izan::ui::draw_custom_menu_bar(chrome, [&] {
            if (ImGui::BeginMenu("View")) {
                for (int i = 0; i < int(izan::ui::kThemeNames.size()); ++i) {
                    const bool selected = chrome.theme_index == i;
                    if (ImGui::MenuItem(
                            izan::ui::kThemeNames[i], nullptr, selected))
                        pending_theme = i;
                }
                ImGui::Separator();
                ImGui::MenuItem("ImGui demo", nullptr, &show_demo);
                ImGui::EndMenu();
            }
        });
        if (pending_theme >= 0) {
            chrome.theme_index = pending_theme;
            izan::ui::apply_theme_style_only(pending_theme);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float top = izan::ui::kWindowFrameMargin
            + izan::ui::kTitleBarHeight + izan::ui::kMenuBarHeight;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + izan::ui::kWindowFrameMargin,
                viewport->Pos.y + top));
        ImGui::SetNextWindowSize(
            ImVec2(viewport->Size.x - izan::ui::kWindowFrameMargin * 2.0f,
                viewport->Size.y - top - izan::ui::kWindowFrameMargin
                    - izan::ui::kStatusBarHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGui::Begin("##dockhost", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoSavedSettings);
        const ImGuiID dockspace = ImGui::GetID("izan-dockspace");
        izan::ui::dock_guard_prepass(dockspace, ImGui::GetContentRegionAvail());
        ImGui::DockSpace(
            dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
        izan::ui::dock_splitter_dblclick_reset(dockspace);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        draw_placeholder_pane("Portfolio", "watch-only balances land here");
        draw_placeholder_pane(
            "Vault", "create / import / unlock flows land here");

        if (show_demo)
            ImGui::ShowDemoWindow(&show_demo);

        izan::ui::draw_status_bar(chrome, "shell harness · all systems go");
        izan::ui::draw_snap_layout_popup(app.window(), chrome);
        izan::ui::draw_menu_popup_shadows(chrome);
        if (chrome.pending_tooltip_visible) {
            izan::ui::draw_simple_tooltip(chrome, "##tooltip",
                chrome.pending_tooltip_text.c_str(),
                chrome.pending_tooltip_anchor);
            chrome.pending_tooltip_visible = false;
        }
        izan::ui::update_ime_position(app.window(), nullptr);

        if (chrome.request_exit)
            glfwSetWindowShouldClose(app.window(), GLFW_TRUE);

        app.end_frame(izan::ui::theme_clear_color(chrome));
    });
    app.run();
    return 0;
}
