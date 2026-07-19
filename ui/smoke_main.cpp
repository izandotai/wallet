// Shell acceptance harness: boots the full izan shell — borderless
// chrome, 15 themes, CJK/emoji fonts, snap layouts, dock guardian —
// with placeholder panes. This is what a human eyeballs after any
// shell or dependency change; the wallet app proper has its own entry
// point.
#include "ui/shell/app.hpp"
#include "ui/shell/chrome_state.hpp"
#include "ui/shell/chrome_widgets.hpp"
#include "ui/shell/constants.hpp"
#include "ui/shell/fonts.hpp"
#include "ui/shell/ime.hpp"
#include "ui/shell/theme.hpp"
#include "ui/shell/ui_layout.hpp"
#include "ui/widgets/kit.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <glaze/glaze.hpp>
#include <imgui.h>

#include <GLFW/glfw3.h>

namespace {

// The harness remembers its theme and layout like the wallet does —
// a design-review surface that resets every launch reviews nothing.
struct SmokeSettings {
    int theme_index = 0;
    int window_w = 0;
    int window_h = 0;
    bool window_maximized = false;
    std::string layout;
    std::vector<float> dock_panes;
};

std::filesystem::path smoke_settings_path()
{
    if (const char* appdata = std::getenv("APPDATA")) {
        const auto dir = std::filesystem::path(appdata) / "izan";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!ec)
            return dir / "smoke.settings.json";
    }
    return izan::ui::executable_dir() / "smoke.settings.json";
}

SmokeSettings load_smoke_settings()
{
    SmokeSettings s;
    std::ifstream f(smoke_settings_path(), std::ios::binary);
    if (f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        SmokeSettings parsed;
        if (!glz::read<glz::opts { .error_on_unknown_keys = false }>(
                parsed, ss.str()))
            s = parsed;
    }
    if (s.theme_index < 0 || s.theme_index >= int(izan::ui::kThemeNames.size()))
        s.theme_index = 0;
    return s;
}

void save_smoke_settings(const SmokeSettings& s)
{
    std::string out;
    if (glz::write<glz::opts { .prettify = true }>(s, out))
        return;
    std::ofstream f(smoke_settings_path(), std::ios::binary | std::ios::trunc);
    f << out;
}

void draw_placeholder_pane(const char* name, const char* line)
{
    ImGui::Begin(name);
    ImGui::TextUnformatted(line);
    ImGui::TextDisabled("中文渲染 ✓  emoji 🎨🔑💰  symbols ★◆①");
    ImGui::End();
}

// The components page: every widget in the kit on one scrolling page,
// with fake data — the design-review surface, same idea as a frontend
// library's component showcase. Layout regressions get caught here, by
// eye, without unlocking anything.
void draw_kit_gallery()
{
    using namespace izan::ui;
    static std::array<std::array<char, 48>, 3> notes {};
    static int account = 0;
    static int preset = 0;
    static bool toggled = true;
    static std::array<char, 48> name {};
    static std::array<char, 256> pass {};
    static std::array<char, 1024> paste {};
    static int chain = 0;
    bool secret_focus = false;
    const float em = ImGui::GetFontSize();

    // Glyph forensics: IZAN_GLYPH_PROBE=1 writes advance widths of the
    // suspects next to the exe — the ruler that settles phantom
    // shoulders like a variation selector billed full fare.
    if (std::getenv("IZAN_GLYPH_PROBE")) {
        static bool probed = false;
        if (!probed) {
            probed = true;
            if (std::FILE* f = std::fopen("glyph_probe.txt", "w")) {
                auto put = [&](const char* name, const char* s) {
                    std::fprintf(
                        f, "%-12s %.2f\n", name, ImGui::CalcTextSize(s).x);
                };
                put("torii+vs16", "⛩️");
                put("torii", "⛩");
                put("vs16", "\xEF\xB8\x8F");
                put("briefcase", "💼");
                put("latin-A", "A");
                std::fclose(f);
            }
        }
    }

    // Screenshot rig: IZAN_GALLERY_SCROLL=<px> parks the page at a
    // given scroll offset so any section can be captured unattended.
    if (const char* jump = std::getenv("IZAN_GALLERY_SCROLL"))
        ImGui::SetNextWindowScroll(ImVec2(0.0f, float(atof(jump))));
    ImGui::Begin("Components");

    kit_title("Components");
    kit_caption("izan kit · 设计语言 Cupertino · 全部组件");
    kit_vspace();

    kit_heading("排版 label");
    kit_group_begin("##sec-type");
    kit_title("标题 Title");
    kit_heading("小标题 Heading");
    ImGui::TextUnformatted("正文 Body");
    kit_caption("说明 Caption");
    kit_group_end();
    kit_vspace();

    kit_heading("按钮 button");
    kit_group_begin("##sec-buttons");
    kit_subtle_button("次要");
    ImGui::SameLine();
    kit_primary_button("主要");
    ImGui::SameLine();
    kit_danger_button("危险");
    ImGui::SameLine();
    kit_link_button("链接动作");
    ImGui::SameLine();
    kit_add_button("##add-demo");
    kit_group_end();
    kit_vspace();

    kit_heading("徽标 pill · avatar");
    kit_group_begin("##sec-badges");
    kit_pill("HD 钱包", kit_accent());
    ImGui::SameLine();
    kit_pill("已锁定", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SameLine();
    kit_avatar("老干妈?", em * 1.7f);
    ImGui::SameLine();
    kit_avatar("Solana", em * 1.7f);
    ImGui::SameLine();
    kit_avatar("main", em * 1.7f);
    kit_group_end();
    kit_vspace();

    kit_heading("表单 text_field · secret · select · toggle");
    kit_group_begin("##sec-forms");
    ImGui::SetNextItemWidth(em * 10.0f);
    kit_text_field("##name", "钱包名称", name.data(), name.size());
    ImGui::SetNextItemWidth(em * 10.0f);
    secret_field("##pass", pass, secret_focus, "口令");
    if (kit_select_begin(
            "##chain", chain == 0 ? "Ethereum" : "Sepolia", em * 10.0f)) {
        if (kit_select_item("Ethereum", chain == 0))
            chain = 0;
        if (kit_select_item("Sepolia", chain == 1))
            chain = 1;
        kit_select_end();
    }
    kit_toggle("##t", &toggled);
    kit_group_end();
    kit_vspace();

    kit_heading("金额输入 amount_field");
    kit_group_begin("##sec-amount");
    static std::array<char, 32> amount {};
    static bool amount_unit = false;
    ImGui::SetNextItemWidth(em * 18.0f);
    bool unit_clicked = false;
    kit_amount_field("##amount", amount.data(), amount.size(),
        amount_unit ? "Solana" : "Ethereum", &unit_clicked);
    if (unit_clicked)
        amount_unit = !amount_unit;
    kit_group_end();
    kit_vspace();

    kit_heading("地址框 address_field · 链接 hyperlink");
    kit_group_begin("##sec-address");
    static std::array<char, 64> addr {};
    ImGui::SetNextItemWidth(em * 18.0f);
    kit_address_field("##addr-demo", "收款地址（粘贴需 0x 开头 42 位）",
        addr.data(), addr.size(), "粘贴", "复制", "清除", [](const char* s) {
            return std::strlen(s) == 42 && s[0] == '0' && s[1] == 'x';
        });
    kit_hyperlink("##link-demo", "etherscan.io/tx/0x43b5…e7",
        "https://etherscan.io/tx/0xdeadbeef");
    kit_group_end();
    kit_vspace();

    kit_heading("粘贴框 paste_box");
    kit_paste_box("##paste", "粘贴助记词、私钥或 WIF", paste.data(),
        paste.size(), 3.0f, secret_focus);
    kit_vspace();

    kit_heading("账户行 selection_mark · copy_text · 余额列 · QR");
    kit_group_begin("##sec-acc");
    static const char* kAddrs[3]
        = { "CNLDm8FPe7HMVnvuNy287zUHjqYtQGnPJgxSup2LMJzD",
              "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
              "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu" };
    static const char* kBals[3] = { "", "0.0012 ETH", "" };
    static int qr_row = -1;
    static bool open_qr = false;
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
        if (*kBals[i]) {
            ImGui::SameLine();
            ImGui::PushFont(nullptr, kit_caption_size());
            ImGui::TextDisabled("%s", kBals[i]);
            ImGui::PopFont();
        }
        ImGui::SameLine();
        kit_copy_text_right("##addr", kAddrs[i], "复制", "已复制", 2.6f);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()
            + ImGui::GetContentRegionAvail().x - em * 2.2f);
        if (kit_subtle_button("QR", em * 2.2f)) {
            qr_row = i;
            open_qr = true;
        }
        ImGui::PopID();
    }
    kit_hairline();
    kit_link_button("+ 派生新地址");
    kit_group_end();
    kit_vspace();

    kit_heading("选择行 choice_row");
    kit_group_begin("##sec-choices");
    if (kit_choice_row("##c0", "MetaMask",
            "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", preset == 0))
        preset = 0;
    kit_hairline();
    if (kit_choice_row("##c1", "BTC Nested SegWit (BIP49)",
            "3D9iyFHi1Zs9KoyynUfrL82rGhJfYTfSG4", preset == 1))
        preset = 1;
    kit_group_end();
    kit_vspace();

    kit_heading("反馈 spinner · step_dots · result_mark");
    kit_group_begin("##sec-feedback");
    kit_spinner();
    ImGui::SameLine();
    kit_step_dots(1, 3);
    kit_result_mark(true, 2.2f);
    kit_result_mark(false, 2.2f);
    kit_group_end();
    kit_vspace();

    kit_heading("身份头 identity");
    kit_group_begin("##sec-identity");
    kit_identity(
        "main", "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "$1,234.56");
    kit_group_end();
    kit_vspace();

    kit_heading("资产行 asset_row");
    kit_group_begin("##sec-assets");
    const AssetRowEvent a0 = kit_asset_row(
        "##a0", "ETH", "Ethereum", "0.0012", true, "", "$4.21", true);
    if (a0.menu)
        ImGui::OpenPopup("##a0-menu");
    if (kit_menu_begin("##a0-menu")) {
        kit_menu_item("发送");
        kit_menu_item("复制合约地址");
        kit_menu_item("在浏览器中查看");
        kit_menu_end();
    }
    kit_hairline();
    kit_asset_row("##a1", "USDC.e", "Polygon", "0.10", true, "", "$0.10");
    kit_hairline();
    kit_asset_row("##a2", "SOL", "Solana", "", false, "链无响应");
    kit_group_end();
    kit_vspace();

    kit_heading("表格 table");
    kit_group_begin("##sec-table");
    if (kit_table_begin("##demo-table", 3)) {
        static const char* kCols[3] = { "链", "资产", "余额" };
        kit_table_headers(kCols, 3);
        for (int r = 0; r < 2; ++r) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r == 0 ? "Ethereum" : "Polygon");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r == 0 ? "ETH" : "USDC.e");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r == 0 ? "0.0012" : "0.10");
        }
        kit_table_end();
    }
    kit_group_end();
    kit_vspace();

    kit_heading("二维码 qr");
    kit_qr("0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", 7.0f);
    kit_vspace();

    kit_heading("对话框 dialog");
    if (kit_primary_button("打开对话框"))
        kit_dialog_open("##demo-dialog");
    if (kit_dialog_begin("##demo-dialog")) {
        kit_dialog_header_avatar("老干妈?", "重命名", "示例说明文字。");
        kit_dialog_field_width();
        kit_text_field("##dname", "钱包名称", name.data(), name.size());
        if (kit_dialog_buttons("取消", "确认") != 0)
            kit_dialog_close();
        kit_dialog_end();
    }
    kit_vspace();

    kit_heading("空状态 empty_state");
    ImGui::BeginChild("##sec-empty", ImVec2(0.0f, em * 7.0f));
    kit_empty_state("👛", "这里还没有内容");
    ImGui::EndChild();

    // The account rows' QR dialog, shared by the section above.
    if (open_qr) {
        kit_dialog_open("##demo-qr");
        open_qr = false;
    }
    if (kit_dialog_begin("##demo-qr")) {
        if (qr_row >= 0) {
            const float side = em * 9.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - side) * 0.5f);
            kit_qr(kAddrs[qr_row], 9.0f);
            kit_vspace(0.4f);
            kit_copy_text_centered(
                "##demo-qr-addr", kAddrs[qr_row], "复制", "已复制");
            kit_vspace(0.3f);
            const float bw = ImGui::CalcTextSize("返回").x
                + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw) * 0.5f);
            if (kit_subtle_button("返回"))
                kit_dialog_close();
        }
        kit_dialog_end();
    }

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

    SmokeSettings settings = load_smoke_settings();
    chrome.theme_index = settings.theme_index;
    izan::ui::apply_theme_style_only(settings.theme_index);
    izan::ui::LayoutKeeper keeper;
    keeper.restore(app.window(),
        { settings.window_w, settings.window_h, settings.window_maximized,
            settings.layout, settings.dock_panes });
    const auto merge = [&](const izan::ui::LayoutState& u) {
        settings.window_w = u.window_w;
        settings.window_h = u.window_h;
        settings.window_maximized = u.window_maximized;
        settings.layout = u.layout;
        settings.dock_panes = u.dock_panes;
        settings.theme_index = chrome.theme_index;
    };

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

        keeper.update(app.window(), dockspace, [&](const auto& u) {
            merge(u);
            save_smoke_settings(settings);
        });

        if (chrome.request_exit)
            glfwSetWindowShouldClose(app.window(), GLFW_TRUE);

        app.end_frame(izan::ui::theme_clear_color(chrome));
    });
    app.run();
    merge(keeper.final_state());
    save_smoke_settings(settings);
    return 0;
}
