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
#include "ui/widgets/kit.hpp"

#include <array>

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

// The kit gallery: every component of the widget library on one page,
// with fake data — the design-review surface. Layout regressions get
// caught here, by eye, without unlocking anything.
void draw_kit_gallery()
{
    using namespace izan::ui;
    static std::array<std::array<char, 48>, 3> notes {};
    static int account = 0;
    static int preset = 0;
    static bool toggled = true;
    static std::array<char, 48> name {};
    static std::array<char, 256> pass {};
    bool secret_focus = false;
    const float em = ImGui::GetFontSize();

    ImGui::Begin("Kit");

    kit_title("账户");
    kit_vspace(0.15f);
    kit_group_begin("##acc");
    static const char* kAddrs[3]
        = { "CNLDm8FPe7HMVnvuNy287zUHjqYtQGnPJgxSup2LMJzD",
              "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
              "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu" };
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        if (i > 0)
            kit_hairline();
        ImGui::AlignTextToFramePadding();
        if (kit_selection_mark("##sel", i == account))
            account = i;
        ImGui::SameLine(em * 1.6f);
        ImGui::PushFont(nullptr, kit_caption_size());
        ImGui::Text("#%d", i);
        ImGui::PopFont();
        ImGui::SameLine(em * 3.2f);
        ImGui::SetNextItemWidth(em * 6.0f);
        kit_text_field("##note", "备注", notes[i].data(), notes[i].size());
        ImGui::SameLine();
        kit_copy_text_right("##addr", kAddrs[i], "复制", "已复制");
        ImGui::PopID();
    }
    kit_hairline();
    kit_link_button("+ 派生新地址");
    kit_group_end();
    kit_vspace();

    kit_heading("选择行");
    kit_group_begin("##choices");
    if (kit_choice_row("##c0", "MetaMask", "0xd8dA6BF2…A96045", preset == 0))
        preset = 0;
    kit_hairline();
    if (kit_choice_row(
            "##c1", "BTC SegWit (BIP84)", "bc1qcr8te4k…306fyu", preset == 1))
        preset = 1;
    kit_group_end();
    kit_vspace();

    kit_heading("控件");
    kit_subtle_button("取消");
    ImGui::SameLine();
    kit_primary_button("确认");
    ImGui::SameLine();
    kit_danger_button("删除");
    ImGui::SameLine();
    kit_link_button("链接动作");
    ImGui::SameLine();
    kit_toggle("##t", &toggled);
    ImGui::SameLine();
    kit_spinner();
    ImGui::SameLine();
    kit_step_dots(1, 3);
    kit_pill("HD 钱包", kit_accent());
    ImGui::SameLine();
    kit_pill("已锁定", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SameLine();
    kit_avatar("老干妈?", em * 1.7f);
    kit_vspace();

    kit_heading("表单");
    ImGui::SetNextItemWidth(em * 10.0f);
    kit_text_field("##name", "钱包名称", name.data(), name.size());
    ImGui::SetNextItemWidth(em * 10.0f);
    secret_field("##pass", pass, secret_focus, "口令");

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
        draw_kit_gallery();

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
