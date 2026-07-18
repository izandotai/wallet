// Izan Wallet, single binary, two personalities: launched plain it is
// the UI; launched with --keyd-child it is the trust plane, and the
// GUI machinery below is never touched (see keyd/child.hpp).
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder: the first-run layout

#include <GLFW/glfw3.h>

#include "keyd/child.hpp"
#include "ui/i18n/catalog.hpp"
#include "ui/pages/history_page.hpp"
#include "ui/pages/portfolio_page.hpp"
#include "ui/pages/send_page.hpp"
#include "ui/pages/vault_page.hpp"
#include "ui/shell/app.hpp"
#include "ui/shell/chrome_state.hpp"
#include "ui/shell/chrome_widgets.hpp"
#include "ui/shell/constants.hpp"
#include "ui/shell/fonts.hpp"
#include "ui/shell/ime.hpp"
#include "ui/shell/theme.hpp"
#include "ui/shell/ui_layout.hpp"

namespace {

using namespace izan;

// Runtime preferences. Losing this file loses a theme choice and a
// window layout, nothing more — it holds no secrets and no money facts.
struct Settings {
    std::string language = "en";
    int theme_index = 0;
    float window_opacity = 0.96f;
    std::string active_wallet;
    // Flattened ui::LayoutState. The shell owns what these mean (see
    // ui/shell/ui_layout.hpp); this file only owns where they live.
    int window_w = 0;
    int window_h = 0;
    bool window_maximized = false;
    std::string layout;
    std::vector<float> dock_panes;
};

// The dock templates the View menu offers. Building one replaces the
// whole arrangement: 0 = workbench (wallets over send in the left
// column, the vault center stage over the assets shelf), 1 = classic
// three columns (vault flanked by wallets and a stacked right rail).
void apply_dock_template(ImGuiID dockspace, const ImVec2& size, int tpl)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);
    ImGuiID center = dockspace;
    if (tpl == 1) {
        const ImGuiID left = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Left, 0.20f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Right, 0.32f, nullptr, &center);
        const ImGuiID right_bottom = ImGui::DockBuilderSplitNode(
            right, ImGuiDir_Down, 0.5f, nullptr, &right);
        ImGui::DockBuilderDockWindow("###wallet-list", left);
        ImGui::DockBuilderDockWindow("###vault-page", center);
        ImGui::DockBuilderDockWindow("###portfolio-page", right);
        ImGui::DockBuilderDockWindow("###history-page", right);
        ImGui::DockBuilderDockWindow("###send-page", right_bottom);
    } else {
        ImGuiID left = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Left, 0.27f, nullptr, &center);
        const ImGuiID left_bottom = ImGui::DockBuilderSplitNode(
            left, ImGuiDir_Down, 0.48f, nullptr, &left);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Down, 0.40f, nullptr, &center);
        ImGui::DockBuilderDockWindow("###wallet-list", left);
        ImGui::DockBuilderDockWindow("###send-page", left_bottom);
        ImGui::DockBuilderDockWindow("###vault-page", center);
        // Assets and the ledger share the bottom shelf as tabs.
        ImGui::DockBuilderDockWindow("###portfolio-page", bottom);
        ImGui::DockBuilderDockWindow("###history-page", bottom);
    }
    ImGui::DockBuilderFinish(dockspace);
}

ui::LayoutState layout_state_of(const Settings& s)
{
    return { s.window_w, s.window_h, s.window_maximized, s.layout,
        s.dock_panes };
}

void merge_layout_state(const ui::LayoutState& u, Settings& s)
{
    s.window_w = u.window_w;
    s.window_h = u.window_h;
    s.window_maximized = u.window_maximized;
    s.layout = u.layout;
    s.dock_panes = u.dock_panes;
}

// Every piece of mutable state — vault, audit ledger, settings — lives
// in %APPDATA%\izan. The exe's own directory is the wrong home for it:
// under Program Files or an MSIX package it is not writable (worse,
// unelevated writes get silently redirected to VirtualStore), and it
// is shared between users. AppData is per-user, always writable, and
// indifferent to where the exe sits or which cwd launched it.
std::filesystem::path state_dir()
{
    if (const char* appdata = std::getenv("APPDATA")) {
        const auto dir = std::filesystem::path(appdata) / "izan";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!ec)
            return dir;
    }
    return ui::executable_dir();
}

std::filesystem::path settings_path()
{
    return state_dir() / "izan.settings.json";
}

void save_settings(const Settings& s)
{
    std::string out;
    if (glz::write<glz::opts { .prettify = true }>(s, out))
        return;
    std::ofstream f(settings_path(), std::ios::binary | std::ios::trunc);
    f << out;
}

Settings load_settings()
{
    const auto read_from = [](const std::filesystem::path& path, Settings& s) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::ostringstream buf;
        buf << f.rdbuf();
        Settings parsed;
        if (glz::read<glz::opts { .error_on_unknown_keys = false }>(
                parsed, buf.str()))
            return false;
        s = parsed;
        return true;
    };

    Settings s;
    if (read_from(settings_path(), s))
        return s;
    // One-time relocation from the old beside-the-exe home; the old
    // file moves rather than lingering as a shadow copy.
    const auto old = ui::executable_dir() / "izan.settings.json";
    if (old != settings_path() && read_from(old, s)) {
        save_settings(s);
        std::error_code ec;
        std::filesystem::remove(old, ec);
    }
    return s;
}

// Wallets live one vault file each under wallets/. The single-vault
// era's izan.qvlt (plus its audit ledger and rotation) moves in as
// "main" once; the move keeps the money history and the ledger chain
// intact.
std::filesystem::path wallets_dir()
{
    const auto dir = state_dir() / "wallets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto old = state_dir() / "izan.qvlt";
    if (std::filesystem::exists(old)
        && !std::filesystem::exists(dir / "main.qvlt")) {
        std::filesystem::rename(old, dir / "main.qvlt", ec);
        for (const char* suffix : { ".qvlt.audit", ".qvlt.bak" }) {
            const auto extra = state_dir() / ("izan" + std::string(suffix));
            if (std::filesystem::exists(extra))
                std::filesystem::rename(
                    extra, dir / ("main" + std::string(suffix)), ec);
        }
    }
    return dir;
}

std::string self_exe_path()
{
    return (ui::executable_dir() / std::filesystem::path("izan.exe")).string();
}

}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--keyd-child")
        return keyd::child_main(argc, argv);

    Settings settings = load_settings();

    const auto langDir = ui::executable_dir() / "assets" / "lang";
    // Preload code→native-name pairs for the language menu.
    std::vector<std::pair<std::string, std::string>> languages;
    for (const std::string& code : i18n::Catalog::available(langDir)) {
        try {
            languages.emplace_back(
                code, i18n::Catalog::load(langDir, code)("lang.name"));
        } catch (const std::exception&) {
            // an unreadable catalog simply stays out of the menu
        }
    }
    i18n::Catalog tr = [&] {
        try {
            return i18n::Catalog::load(langDir, settings.language);
        } catch (const std::exception&) {
            return i18n::Catalog::load(langDir, i18n::kBaseLanguage);
        }
    }();

    ui::GlfwApp app;
    ui::AppOptions options;
    options.title = "izan";
    options.width = 1600;
    options.height = 900;
    if (!app.init(options))
        return 1;

    ui::LayoutKeeper layout_keeper;
    layout_keeper.restore(app.window(), layout_state_of(settings));

    ui::ChromeState chrome;
    chrome.theme_index = settings.theme_index;
    chrome.window_opacity = settings.window_opacity;
    ui::apply_theme_style_only(chrome.theme_index);
    glfwSetWindowOpacity(app.window(), chrome.window_opacity);

    ui::VaultPage vault(wallets_dir(), ui::executable_dir() / "data",
        self_exe_path(), settings.active_wallet);

    // A broken chain/token config takes down the portfolio pane, not
    // the wallet: the vault stays reachable and the error is shown.
    std::optional<ui::PortfolioPage> portfolio;
    std::string portfolioError;
    try {
        portfolio.emplace(ui::executable_dir() / "data", state_dir(), vault);
    } catch (const std::exception& e) {
        portfolioError = e.what();
    }

    // Same containment for the send pane's chain registry.
    std::optional<ui::SendPage> send;
    std::string sendError;
    try {
        send.emplace(ui::executable_dir() / "data", state_dir(), vault);
    } catch (const std::exception& e) {
        sendError = e.what();
    }

    // Touching a holding on the assets page walks it to the send form.
    if (portfolio && send)
        portfolio->on_send([&send](uint64_t chain_id, const std::string& sym) {
            send->prefill(chain_id, sym);
        });

    // The ledger pane; same containment as its siblings.
    std::optional<ui::HistoryPage> history;
    std::string historyError;
    try {
        history.emplace(ui::executable_dir() / "data", vault);
    } catch (const std::exception& e) {
        historyError = e.what();
    }

    app.set_render_callback([&] {
        app.begin_frame();

        ui::draw_main_window_frame(chrome);
        ui::draw_custom_title_bar(app.window(), chrome, tr("app.title"),
            vault.unlocked() ? tr("vault.state.unlocked")
                             : tr("vault.state.locked"));
        // Theme application is deferred to after the menu bar returns:
        // draw_custom_menu_bar wraps the items in Push/PopStyleColor
        // pairs, and popping would write the old theme's colors right
        // back over a style applied mid-callback.
        int pending_theme = -1;
        int pending_layout = -1;
        ui::draw_custom_menu_bar(chrome, [&] {
            if (ImGui::BeginMenu(tr("menu.view"))) {
                if (ImGui::BeginMenu(tr("menu.layout"))) {
                    if (ImGui::MenuItem(tr("layout.workbench")))
                        pending_layout = 0;
                    if (ImGui::MenuItem(tr("layout.classic")))
                        pending_layout = 1;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("menu.theme"))) {
                    for (int i = 0; i < int(ui::kThemeNames.size()); ++i) {
                        if (ImGui::MenuItem(ui::kThemeNames[i], nullptr,
                                chrome.theme_index == i))
                            pending_theme = i;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("menu.language"))) {
                    for (const auto& [code, name] : languages) {
                        if (ImGui::MenuItem(
                                name.c_str(), nullptr, tr.language() == code)) {
                            try {
                                tr = i18n::Catalog::load(langDir, code);
                                settings.language = code;
                                save_settings(settings);
                            } catch (const std::exception&) {
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
        });
        if (pending_theme >= 0) {
            chrome.theme_index = pending_theme;
            ui::apply_theme_style_only(pending_theme);
            settings.theme_index = pending_theme;
            save_settings(settings);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float top
            = ui::kWindowFrameMargin + ui::kTitleBarHeight + ui::kMenuBarHeight;
        ImGui::SetNextWindowPos(ImVec2(
            viewport->Pos.x + ui::kWindowFrameMargin, viewport->Pos.y + top));
        ImGui::SetNextWindowSize(
            ImVec2(viewport->Size.x - ui::kWindowFrameMargin * 2.0f,
                viewport->Size.y - top - ui::kWindowFrameMargin
                    - ui::kStatusBarHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGui::Begin("##dockhost", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoSavedSettings);
        const ImGuiID dockspace = ImGui::GetID("izan-dockspace");
        const ImVec2 dock_size = ImGui::GetContentRegionAvail();
        // First run (no saved layout): build the workbench template.
        // Every run after this belongs to the user's own arrangement —
        // until they pick a template from the View menu, which rebuilds
        // on the spot. Built on the second frame at the earliest: the
        // windows must have submitted once or their empty dock nodes
        // collapse before they arrive.
        static int dock_frame = 0;
        if (dock_frame < 2 && ++dock_frame == 2 && settings.layout.empty())
            pending_layout = 0;
        if (pending_layout >= 0 && dock_frame >= 2) {
            ui::dock_ledger_import({}); // the old anchors died with the tree
            apply_dock_template(dockspace, dock_size, pending_layout);
        }
        // A window added in a newer build floats over a layout saved
        // by an older one; adopt the ledger into the assets shelf,
        // where the templates would have put it.
        static bool adopted = false;
        if (!adopted && dock_frame == 2 && !settings.layout.empty()) {
            adopted = true;
            ImGuiWindow* ledger = ImGui::FindWindowByName("###history-page");
            ImGuiWindow* shelf = ImGui::FindWindowByName("###portfolio-page");
            if (ledger && shelf && ledger->DockId == 0 && shelf->DockId != 0)
                ImGui::DockBuilderDockWindow("###history-page", shelf->DockId);
        }
        static int dump_frame = 0;
        ++dump_frame;
        if ((dump_frame == 3 || dump_frame == 240)
            && std::getenv("IZAN_DOCK_DUMP")) {
            std::ofstream dump(
                dump_frame == 3 ? "izan-dock-dump.txt" : "izan-dock-dump2.txt");
            for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
                dump << w->Name << " id=" << w->ID << " dock=" << w->DockId
                     << " active=" << int(w->WasActive) << " pos=" << w->Pos.x
                     << "," << w->Pos.y << " size=" << w->Size.x << "x"
                     << w->Size.y << "\n";
            for (ImGuiID nid : { dockspace, ImGuiID(5), ImGuiID(6) }) {
                ImGuiDockNode* n = ImGui::DockBuilderGetNode(nid);
                dump << "node " << nid << (n ? " alive" : " GONE");
                if (n)
                    dump << " pos=" << n->Pos.x << "," << n->Pos.y
                         << " size=" << n->Size.x << "x" << n->Size.y
                         << " windows=" << n->Windows.Size;
                dump << "\n";
            }
        }
        ui::dock_guard_prepass(dockspace, dock_size);
        ImGui::DockSpace(
            dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
        ui::dock_splitter_dblclick_reset(dockspace);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        vault.draw(app.window(), tr);
        if (send) {
            send->draw(app.window(), tr);
        } else {
            ImGui::Begin(
                (std::string(tr("send.title")) + "###send-page").c_str());
            ImGui::TextWrapped("%s", sendError.c_str());
            ImGui::End();
        }
        if (portfolio) {
            portfolio->draw(tr);
        } else {
            ImGui::Begin(
                (std::string(tr("portfolio.title")) + "###portfolio-page")
                    .c_str());
            ImGui::TextWrapped("%s", portfolioError.c_str());
            ImGui::End();
        }
        if (history) {
            history->draw(tr);
        } else {
            ImGui::Begin(
                (std::string(tr("history.title")) + "###history-page").c_str());
            ImGui::TextWrapped("%s", historyError.c_str());
            ImGui::End();
        }

        ui::draw_status_bar(chrome, tr("status.ready"));
        ui::draw_snap_layout_popup(app.window(), chrome);
        ui::draw_menu_popup_shadows(chrome);
        if (chrome.pending_tooltip_visible) {
            ui::draw_simple_tooltip(chrome, "##tooltip",
                chrome.pending_tooltip_text.c_str(),
                chrome.pending_tooltip_anchor);
            chrome.pending_tooltip_visible = false;
        }
        ui::update_ime_position(app.window(), nullptr);

        if (chrome.window_opacity != settings.window_opacity) {
            settings.window_opacity = chrome.window_opacity;
            save_settings(settings);
        }
        if (vault.active() != settings.active_wallet) {
            settings.active_wallet = vault.active();
            save_settings(settings);
        }
        layout_keeper.update(
            app.window(), dockspace, [&](const ui::LayoutState& u) {
                merge_layout_state(u, settings);
                save_settings(settings);
            });
        if (chrome.request_exit)
            glfwSetWindowShouldClose(app.window(), GLFW_TRUE);

        app.end_frame(ui::theme_clear_color(chrome));
    });
    app.run();
    merge_layout_state(layout_keeper.final_state(), settings);
    save_settings(settings);
    return 0;
}
