#include "ui/shell/ui_layout.hpp"

#include "ui/shell/fonts.hpp"
#include "ui/shell/win_chrome.hpp"

#include <imgui_internal.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace izan::ui {

void toggle_window_maximized(GLFWwindow* window)
{
    if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE)
        glfwRestoreWindow(window);
    else
        glfwMaximizeWindow(window);
}

void snap_window_to_work_area(GLFWwindow* window, float x_fraction,
    float y_fraction, float width_fraction, float height_fraction)
{
    const WorkArea area = current_window_work_area(window);
    if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE)
        glfwRestoreWindow(window);

    const int x = area.x
        + static_cast<int>(
            std::lround(static_cast<float>(area.width) * x_fraction));
    const int y = area.y
        + static_cast<int>(
            std::lround(static_cast<float>(area.height) * y_fraction));
    const int right = area.x
        + static_cast<int>(std::lround(
            static_cast<float>(area.width) * (x_fraction + width_fraction)));
    const int bottom = area.y
        + static_cast<int>(std::lround(
            static_cast<float>(area.height) * (y_fraction + height_fraction)));
    const int width = std::max(320, right - x);
    const int height = std::max(240, bottom - y);
    set_window_visible_bounds(window, x, y, width, height);
}

void center_window_on_work_area(GLFWwindow* window)
{
    const WorkArea area = current_window_work_area(window);
    int w = 0, h = 0;
    glfwGetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0 || area.width <= 0 || area.height <= 0)
        return;
    set_window_visible_bounds(window,
        area.x + std::max(0, (area.width - w) / 2),
        area.y + std::max(0, (area.height - h) / 2), w, h);
}

// ---- Dock pane guardian ----
// Panels keep their pixels, the content flexes: resizing or maximizing
// the window must not scale a sidebar, exactly like every desktop
// IDE. The ledger records the pixel size of each split's anchored pane
// (the side NOT containing the central node); enforcement holds those
// pixels against imgui's allocation drift, only the dragged splitter
// learns, and a double-click resets that splitter to an even split.

namespace {

    constexpr float kMinPanePx = 60.0f;

    std::unordered_map<unsigned int, float> g_dock_px; // node → anchor px

    bool dock_is_split(ImGuiDockNode* n)
    {
        return n != nullptr && n->ChildNodes[0] != nullptr
            && n->ChildNodes[1] != nullptr && n->ChildNodes[0]->IsVisible
            && n->ChildNodes[1]->IsVisible;
    }

    bool dock_contains_central(ImGuiDockNode* n)
    {
        if (n == nullptr)
            return false;
        if ((n->MergedFlags | n->LocalFlags) & ImGuiDockNodeFlags_CentralNode)
            return true;
        return dock_contains_central(n->ChildNodes[0])
            || dock_contains_central(n->ChildNodes[1]);
    }

    // The anchored pane: the child that does not lead to the central
    // node. Inside a fully fixed subtree neither does; child 0 anchors
    // by convention.
    int dock_anchor_index(ImGuiDockNode* n)
    {
        return dock_contains_central(n->ChildNodes[0]) ? 1 : 0;
    }

    float dock_clamp_px(float px, float total)
    {
        return ImClamp(px, kMinPanePx, ImMax(total - kMinPanePx, kMinPanePx));
    }

    float dock_cur_px(ImGuiDockNode* n)
    {
        const int axis = int(n->SplitAxis);
        return (&n->ChildNodes[dock_anchor_index(n)]->Size.x)[axis];
    }

    void dock_apply_px(ImGuiDockNode* n, float px)
    {
        ImGuiDockNode* anchor = n->ChildNodes[dock_anchor_index(n)];
        ImGuiDockNode* flex = n->ChildNodes[1 - dock_anchor_index(n)];
        const int axis = int(n->SplitAxis);
        const float total = (&anchor->Size.x)[axis] + (&flex->Size.x)[axis];
        px = dock_clamp_px(px, total);
        (&anchor->Size.x)[axis] = (&anchor->SizeRef.x)[axis] = px;
        (&flex->Size.x)[axis] = (&flex->SizeRef.x)[axis] = total - px;
    }

    // The node owning the seam under the mouse (±8px band); shared by
    // double-click reset and drag learning.
    ImGuiDockNode* dock_seam_hit(ImGuiDockNode* n, const ImVec2& m)
    {
        if (n == nullptr)
            return nullptr;
        if (dock_is_split(n)) {
            ImGuiDockNode* a = n->ChildNodes[0];
            ImGuiDockNode* b = n->ChildNodes[1];
            ImRect seam;
            if (n->SplitAxis == ImGuiAxis_X) {
                seam.Min = ImVec2(
                    a->Pos.x + a->Size.x - 8.0f, ImMax(a->Pos.y, b->Pos.y));
                seam.Max = ImVec2(b->Pos.x + 8.0f,
                    ImMin(a->Pos.y + a->Size.y, b->Pos.y + b->Size.y));
            } else {
                seam.Min = ImVec2(
                    ImMax(a->Pos.x, b->Pos.x), a->Pos.y + a->Size.y - 8.0f);
                seam.Max
                    = ImVec2(ImMin(a->Pos.x + a->Size.x, b->Pos.x + b->Size.x),
                        b->Pos.y + 8.0f);
            }
            if (seam.Contains(m))
                return n;
        }
        if (ImGuiDockNode* r = dock_seam_hit(n->ChildNodes[0], m))
            return r;
        return dock_seam_hit(n->ChildNodes[1], m);
    }

    // Double-click resets only the clicked splitter. Resetting the
    // whole subtree was tried and reverted: with the guardian active no
    // drag can skew other splitters anymore, and the collateral reset
    // clobbered panes the user had deliberately collapsed.
    void dock_reset_even(ImGuiDockNode* n)
    {
        if (n == nullptr || !dock_is_split(n))
            return;
        const int axis = int(n->SplitAxis);
        const float total = (&n->ChildNodes[0]->Size.x)[axis]
            + (&n->ChildNodes[1]->Size.x)[axis];
        g_dock_px[n->ID] = total / 2.0f;
    }

    // Rewrites SizeRef through the tree for a given node size: the
    // anchored pane keeps its ledger pixels, the flexing side takes
    // the rest.
    void dock_prepass_apply(ImGuiDockNode* n, ImVec2 avail)
    {
        if (n == nullptr)
            return;
        if (!dock_is_split(n)) {
            dock_prepass_apply(n->ChildNodes[0], avail);
            dock_prepass_apply(n->ChildNodes[1], avail);
            return;
        }
        const int anchorAt = dock_anchor_index(n);
        ImGuiDockNode* anchor = n->ChildNodes[anchorAt];
        ImGuiDockNode* flex = n->ChildNodes[1 - anchorAt];
        const int axis = int(n->SplitAxis);
        const float spacing = ImGui::GetStyle().DockingSeparatorSize;
        const float total = ImMax((&avail.x)[axis] - spacing, 0.0f);
        const auto it = g_dock_px.find(n->ID);
        const float px = dock_clamp_px(
            it != g_dock_px.end() ? it->second : dock_cur_px(n), total);
        (&anchor->SizeRef.x)[axis] = px;
        (&flex->SizeRef.x)[axis] = total - px;
        ImVec2 availAnchor = avail, availFlex = avail;
        (&availAnchor.x)[axis] = px;
        (&availFlex.x)[axis] = total - px;
        dock_prepass_apply(anchor, availAnchor);
        dock_prepass_apply(flex, availFlex);
    }

    // Pane sizes staged by import, waiting for their split nodes to
    // materialize; indexed by depth-first split order.
    std::vector<float> g_dock_pending;

    // Structural split test — children exist, visibility ignored. At
    // startup the ledger must be seeded before any window has been
    // submitted, when nothing is visible yet.
    bool dock_is_split_structural(ImGuiDockNode* n)
    {
        return n != nullptr && n->ChildNodes[0] != nullptr
            && n->ChildNodes[1] != nullptr;
    }

    // First meeting with a split the session holds no opinion on:
    // install the imported pane size for its depth-first position,
    // falling back to the pixels the ini restored into SizeRef.
    // Without this an unseeded ledger learns whatever imgui's restore
    // produced: the "sidebars snap back to half on every launch" bug.
    // Values below 1.0 are ratios from the retired scheme and convert
    // against the restored total once. Nothing here overwrites what a
    // drag has already taught this session.
    void dock_seed_ledger(ImGuiDockNode* n, std::size_t& k)
    {
        if (n == nullptr)
            return;
        if (dock_is_split_structural(n)) {
            if (!g_dock_px.contains(n->ID)) {
                const int axis = int(n->SplitAxis);
                const float sa
                    = (&n->ChildNodes[dock_anchor_index(n)]->SizeRef.x)[axis];
                const float sb = (&n->ChildNodes[1 - dock_anchor_index(n)]
                        ->SizeRef.x)[axis];
                float pending
                    = k < g_dock_pending.size() ? g_dock_pending[k] : 0.0f;
                if (pending > 0.02f && pending < 1.0f)
                    pending *= sa + sb; // legacy ratio wire form
                if (pending >= kMinPanePx)
                    g_dock_px.emplace(n->ID, pending);
                else if (sa >= 1.0f)
                    g_dock_px.emplace(n->ID, sa);
            }
            ++k;
        }
        dock_seed_ledger(n->ChildNodes[0], k);
        dock_seed_ledger(n->ChildNodes[1], k);
    }

    void dock_collect_panes(ImGuiDockNode* n, std::vector<float>& out)
    {
        if (n == nullptr)
            return;
        if (dock_is_split_structural(n)) {
            const auto it = g_dock_px.find(n->ID);
            out.push_back(it != g_dock_px.end() ? it->second : dock_cur_px(n));
        }
        dock_collect_panes(n->ChildNodes[0], out);
        dock_collect_panes(n->ChildNodes[1], out);
    }

    void dock_walk_keep(ImGuiDockNode* n, ImGuiDockNode* learning)
    {
        if (n == nullptr)
            return;
        if (dock_is_split(n)) {
            const float cur = dock_cur_px(n);
            auto it = g_dock_px.find(n->ID);
            if (n == learning || it == g_dock_px.end()) {
                g_dock_px[n->ID] = cur;       // dragged or first seen: learn
            } else if (cur < it->second - 0.5f || cur > it->second + 0.5f) {
                dock_apply_px(n, it->second); // others: hold the ledger
            }
        }
        dock_walk_keep(n->ChildNodes[0], learning);
        dock_walk_keep(n->ChildNodes[1], learning);
    }

}

std::vector<float> dock_ledger_export(unsigned int dockspace_id)
{
    std::vector<float> out;
    dock_collect_panes(
        ImGui::DockBuilderGetNode(static_cast<ImGuiID>(dockspace_id)), out);
    return out;
}

void dock_ledger_import(const std::vector<float>& panes)
{
    g_dock_pending = panes;
}

void dock_guard_prepass(unsigned int dockspace_id, const ImVec2& size)
{
    ImGuiDockNode* root
        = ImGui::DockBuilderGetNode(static_cast<ImGuiID>(dockspace_id));
    if (root == nullptr)
        return;
    std::size_t k = 0;
    dock_seed_ledger(root, k);
    if (root->Size.x == size.x && root->Size.y == size.y)
        return; // host size stable: the post-pass ledger is in charge
    dock_prepass_apply(root, size);
}

void dock_splitter_dblclick_reset(unsigned int dockspace_id)
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    ImGuiDockNode* root
        = ImGui::DockBuilderGetNode(static_cast<ImGuiID>(dockspace_id));
    if (ctx == nullptr || root == nullptr)
        return;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    // Splitter interaction in flight? (SplitterBehavior holds ActiveId
    // and its window hosts a dock node.)
    const bool on_splitter = ctx->ActiveIdWindow != nullptr
        && ctx->ActiveIdWindow->DockNodeAsHost != nullptr;
    ImGuiDockNode* seam = on_splitter ? dock_seam_hit(root, mouse) : nullptr;
    if (on_splitter && seam != nullptr
        && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        dock_reset_even(seam);
        ImGui::ClearActiveID();
        seam = nullptr; // enforce the fresh ledger this same frame
        ImGui::MarkIniSettingsDirty();
    }
    dock_walk_keep(root, seam);
}

namespace {

    // One-time adoption of a layout left by the retired beside-the-exe
    // ini scheme; the file is consumed, not left to shadow the real
    // store.
    void adopt_legacy_ini(LayoutState& state)
    {
        const auto path = executable_dir() / "izan.imgui.ini";
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return;
        std::ostringstream buf;
        buf << f.rdbuf();
        state.layout = buf.str();
        f.close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

}

void LayoutKeeper::restore(GLFWwindow* window, const LayoutState& state)
{
    m_state = state;
    if (m_state.layout.empty())
        adopt_legacy_ini(m_state);
    if (!m_state.layout.empty())
        ImGui::LoadIniSettingsFromMemory(
            m_state.layout.data(), m_state.layout.size());
    dock_ledger_import(m_state.dock_panes);

    // Size through the raw-rect call: glfw's decoration math no longer
    // matches the borderless chrome and would grow the frame a border's
    // width per launch.
    if (m_state.window_w > 0 && m_state.window_h > 0) {
        const WorkArea wa = current_window_work_area(window);
        const int w = std::min(m_state.window_w, wa.width);
        const int h = std::min(m_state.window_h, wa.height);
        set_window_screen_rect(window, wa.x + (wa.width - w) / 2,
            wa.y + (wa.height - h) / 2, w, h);
    }
    if (m_state.window_maximized)
        glfwMaximizeWindow(window);
}

void LayoutKeeper::update(GLFWwindow* window, unsigned int dockspace_id,
    const std::function<void(const LayoutState&)>& save)
{
    m_dockspace = dockspace_id;
    bool want_save = false;

    // imgui raises this (throttled by its settings timer) whenever the
    // layout changed; with no ini file of its own, persisting is ours.
    if (ImGui::GetIO().WantSaveIniSettings) {
        ImGui::GetIO().WantSaveIniSettings = false;
        m_state.layout = ImGui::SaveIniSettingsToMemory();
        m_state.dock_panes = dock_ledger_export(dockspace_id);
        want_save = true;
    }

    // Geometry debounces: a border drag fires every frame, the store
    // hears about it once the shape holds for a second (the exit save
    // catches whatever is still pending).
    const bool maxed = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
    bool changed = maxed != m_state.window_maximized;
    m_state.window_maximized = maxed;
    if (!maxed) {
        int w = 0, h = 0;
        glfwGetWindowSize(window, &w, &h);
        if (w > 0 && h > 0
            && (w != m_state.window_w || h != m_state.window_h)) {
            m_state.window_w = w;
            m_state.window_h = h;
            changed = true;
        }
    }
    if (changed)
        m_geometry_dirty_since = glfwGetTime();
    if (m_geometry_dirty_since >= 0.0
        && glfwGetTime() - m_geometry_dirty_since > 1.0) {
        m_geometry_dirty_since = -1.0;
        want_save = true;
    }

    if (want_save)
        save(m_state);
}

const LayoutState& LayoutKeeper::final_state()
{
    m_state.layout = ImGui::SaveIniSettingsToMemory();
    m_state.dock_panes = dock_ledger_export(m_dockspace);
    return m_state;
}

}
