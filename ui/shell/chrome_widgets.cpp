#include "ui/shell/chrome_widgets.hpp"

#include "ui/shell/constants.hpp"
#include "ui/shell/theme.hpp"
#include "ui/shell/ui_layout.hpp"

#include <imgui_internal.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace izan::ui {

namespace {

    bool rects_overlap(const ImRect& lhs, const ImRect& rhs)
    {
        return lhs.Min.x < rhs.Max.x && lhs.Max.x > rhs.Min.x
            && lhs.Min.y < rhs.Max.y && lhs.Max.y > rhs.Min.y;
    }

    void append_rect_minus_blocker(
        std::vector<ImRect>& output, const ImRect& rect, const ImRect& blocker)
    {
        if (!rects_overlap(rect, blocker)) {
            output.push_back(rect);
            return;
        }

        const float ix0 = std::max(rect.Min.x, blocker.Min.x);
        const float iy0 = std::max(rect.Min.y, blocker.Min.y);
        const float ix1 = std::min(rect.Max.x, blocker.Max.x);
        const float iy1 = std::min(rect.Max.y, blocker.Max.y);

        if (rect.Min.y < iy0)
            output.emplace_back(
                ImVec2(rect.Min.x, rect.Min.y), ImVec2(rect.Max.x, iy0));
        if (iy1 < rect.Max.y)
            output.emplace_back(
                ImVec2(rect.Min.x, iy1), ImVec2(rect.Max.x, rect.Max.y));
        if (rect.Min.x < ix0)
            output.emplace_back(ImVec2(rect.Min.x, iy0), ImVec2(ix0, iy1));
        if (ix1 < rect.Max.x)
            output.emplace_back(ImVec2(ix1, iy0), ImVec2(rect.Max.x, iy1));
    }

    void draw_shadow_rect_clipped(ImDrawList* draw_list, const ImRect& rect,
        const std::vector<ImRect>& blockers, ImU32 color, float rounding,
        ImDrawFlags flags)
    {
        std::vector<ImRect> segments { rect };
        for (const ImRect& blocker : blockers) {
            std::vector<ImRect> next_segments;
            for (const ImRect& segment : segments)
                append_rect_minus_blocker(next_segments, segment, blocker);
            segments = std::move(next_segments);
            if (segments.empty())
                return;
        }

        for (const ImRect& segment : segments) {
            if (segment.GetWidth() <= 0.0f || segment.GetHeight() <= 0.0f)
                continue;
            draw_list->AddRectFilled(
                segment.Min, segment.Max, color, rounding, flags);
        }
    }

    void draw_popup_shadow_for_rect(ImDrawList* draw_list,
        const ChromeState& app, const ImRect& rect,
        const std::vector<ImRect>& blockers, bool modal)
    {
        // Two depth profiles. Menus float just off the surface; modals
        // hang in the air the way macOS alerts do — a wide, soft,
        // slightly dropped umbra (roughly ~50px blur, ~30% black,
        // settled a few pixels below the window).
        constexpr std::array<float, 12> kMenuAlpha
            = { 0.020f, 0.017f, 0.014f, 0.011f, 0.0085f, 0.0065f, 0.0050f,
                  0.0038f, 0.0028f, 0.0020f, 0.0014f, 0.0010f };
        constexpr std::array<float, 14> kModalAlpha
            = { 0.032f, 0.028f, 0.024f, 0.020f, 0.016f, 0.013f, 0.010f, 0.008f,
                  0.006f, 0.0045f, 0.0033f, 0.0024f, 0.0017f, 0.0011f };
        const float* alpha = modal ? kModalAlpha.data() : kMenuAlpha.data();
        const int layers
            = modal ? int(kModalAlpha.size()) : int(kMenuAlpha.size());
        const float spread_step = modal ? 3.4f : 1.75f;
        const float spread_base = modal ? 3.0f : 2.0f;
        const float drop = modal ? 4.0f : 0.0f;

        const float base_rounding = ImGui::GetStyle().PopupRounding;
        for (int index = layers - 1; index >= 0; --index) {
            const float layer = static_cast<float>(index + 1);
            const float spread = spread_base + layer * spread_step;
            const ImU32 color = theme_popup_shadow_color(app, alpha[index]);
            const float rounding = base_rounding + spread;

            draw_shadow_rect_clipped(draw_list,
                ImRect(ImVec2(rect.Min.x - spread, rect.Min.y - spread + drop),
                    ImVec2(rect.Max.x + spread, rect.Max.y + spread + drop)),
                blockers, color, rounding, 0);
        }
    }

    void draw_snap_preview(const ChromeState& app, ImDrawList* draw_list,
        const ImVec2& min, const ImVec2& max,
        const std::vector<ImVec4>& regions, bool hovered)
    {
        const ImU32 border = theme_snap_preview_border_color(app, hovered);
        const ImU32 fill = theme_snap_preview_fill_color(app, hovered);
        draw_list->AddRectFilled(
            min, max, theme_snap_preview_background_color(app), 4.0f);
        draw_list->AddRect(min, max, border, 4.0f, 0, 1.5f);
        for (const ImVec4& region : regions) {
            const ImVec2 region_min(min.x + (max.x - min.x) * region.x,
                min.y + (max.y - min.y) * region.y);
            const ImVec2 region_max(min.x + (max.x - min.x) * region.z,
                min.y + (max.y - min.y) * region.w);
            draw_list->AddRectFilled(region_min, region_max, fill, 2.0f);
            draw_list->AddRect(region_min, region_max, border, 2.0f, 0, 1.0f);
        }
    }

    bool snap_layout_item(const ChromeState& app, const char* id,
        const ImVec2& size, const std::vector<ImVec4>& regions)
    {
        ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        draw_snap_preview(app, ImGui::GetWindowDrawList(),
            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), regions, hovered);
        return ImGui::IsItemClicked();
    }

}

void draw_menu_popup_shadows(const ChromeState& app)
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr)
        return;

    struct FloatingRect {
        ImGuiWindow* window = nullptr;
        ImRect rect;
        bool modal = false;
    };

    std::vector<FloatingRect> popup_rects;
    for (ImGuiWindow* window : context->Windows) {
        if (window == nullptr || !window->WasActive)
            continue;
        const bool popup = (window->Flags & ImGuiWindowFlags_Popup) != 0;
        const bool modal = (window->Flags & ImGuiWindowFlags_Modal) != 0;
        const bool tooltip = (window->Flags & ImGuiWindowFlags_Tooltip) != 0;
        if ((!popup && !modal) || tooltip)
            continue;
        popup_rects.push_back({ window,
            ImRect(window->Pos,
                ImVec2(window->Pos.x + window->Size.x,
                    window->Pos.y + window->Size.y)),
            modal });
    }
    if (popup_rects.empty())
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBackground;
    ImGui::Begin("izan-popup-shadow-layer", nullptr, flags);
    ImGuiWindow* shadow_window = ImGui::GetCurrentWindow();
    if (shadow_window != nullptr)
        ImGui::BringWindowToDisplayBehind(
            shadow_window, popup_rects.front().window);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRectFullScreen();

    for (std::size_t index = 0; index < popup_rects.size(); ++index) {
        std::vector<ImRect> blockers;
        for (std::size_t blocker_index = 0; blocker_index < popup_rects.size();
            ++blocker_index) {
            if (blocker_index != index)
                blockers.push_back(popup_rects[blocker_index].rect);
        }
        draw_popup_shadow_for_rect(draw_list, app, popup_rects[index].rect,
            blockers, popup_rects[index].modal);
    }
    draw_list->PopClipRect();
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void draw_main_window_frame(const ChromeState& app)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList(viewport);
    const ImVec2 min(viewport->Pos.x + kWindowFrameMargin,
        viewport->Pos.y + kWindowFrameMargin);
    const ImVec2 max(viewport->Pos.x + viewport->Size.x - kWindowFrameMargin,
        viewport->Pos.y + viewport->Size.y - kWindowFrameMargin);
    const ImVec2 chrome_max(max.x, min.y + kTitleBarHeight + kMenuBarHeight);

    draw_list->AddRectFilled(min, max, theme_frame_background_color(app), 8.0f);
    draw_list->AddRectFilled(min, chrome_max,
        theme_chrome_background_color(app), 8.0f, ImDrawFlags_RoundCornersTop);
    draw_list->AddLine(ImVec2(min.x, min.y + kTitleBarHeight),
        ImVec2(max.x, min.y + kTitleBarHeight),
        theme_chrome_line_color(app, 200), 1.0f);
    draw_list->AddLine(ImVec2(min.x, min.y + kTitleBarHeight + kMenuBarHeight),
        ImVec2(max.x, min.y + kTitleBarHeight + kMenuBarHeight),
        theme_chrome_line_color(app, 180), 1.0f);
}

bool draw_window_control_button(
    const char* id, const ImVec2& size, WindowControlIcon icon)
{
    const bool clicked = ImGui::Button(id, size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const float cx = (min.x + max.x) * 0.5f;
    const float cy = (min.y + max.y) * 0.5f;
    const float stroke = 2.0f;

    switch (icon) {
    case WindowControlIcon::Minimize:
        draw_list->AddLine(ImVec2(cx - 9.0f, cy + 7.0f),
            ImVec2(cx + 9.0f, cy + 7.0f), color, stroke);
        break;
    case WindowControlIcon::Maximize:
        draw_list->AddRect(ImVec2(cx - 9.0f, cy - 9.0f),
            ImVec2(cx + 9.0f, cy + 9.0f), color, 0.0f, 0, stroke);
        break;
    case WindowControlIcon::Restore:
        draw_list->AddRect(ImVec2(cx - 6.0f, cy - 10.0f),
            ImVec2(cx + 10.0f, cy + 6.0f), color, 0.0f, 0, stroke);
        draw_list->AddRect(ImVec2(cx - 10.0f, cy - 6.0f),
            ImVec2(cx + 6.0f, cy + 10.0f), color, 0.0f, 0, stroke);
        break;
    case WindowControlIcon::Close:
        draw_list->AddLine(ImVec2(cx - 8.0f, cy - 8.0f),
            ImVec2(cx + 8.0f, cy + 8.0f), color, stroke);
        draw_list->AddLine(ImVec2(cx + 8.0f, cy - 8.0f),
            ImVec2(cx - 8.0f, cy + 8.0f), color, stroke);
        break;
    }

    return clicked;
}

void draw_simple_tooltip(const ChromeState& app, const char* id,
    const char* text, const ImVec2& anchor)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 padding(10.0f, 6.0f);
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 tooltip_size(
        text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
    ImVec2 tooltip_pos(anchor.x - tooltip_size.x * 0.5f, anchor.y);
    tooltip_pos.x = std::clamp(tooltip_pos.x, viewport->Pos.x + 8.0f,
        viewport->Pos.x + viewport->Size.x - tooltip_size.x - 8.0f);
    tooltip_pos.y = std::clamp(tooltip_pos.y, viewport->Pos.y + 8.0f,
        viewport->Pos.y + viewport->Size.y - tooltip_size.y - 8.0f);

    ImGui::SetNextWindowPos(tooltip_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(tooltip_size, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    ImGui::PushStyleColor(ImGuiCol_Border, theme_chrome_line_color(app, 180));
    ImGui::Begin(id, nullptr, flags);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::TextUnformatted(text);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void queue_simple_tooltip(
    ChromeState& app, const char* text, const ImVec2& anchor)
{
    app.pending_tooltip_visible = true;
    app.pending_tooltip_text = text;
    app.pending_tooltip_anchor = anchor;
}

void draw_snap_layout_popup(GLFWwindow* window, ChromeState& app)
{
    if (!app.snap_layout_open)
        return;

    // The frame is sized from its contents: 4 columns × 2 rows plus the
    // inter-row Dummy(10).
    const ImVec2 item_size(70.0f, 52.0f);
    const float gap = 12.0f;
    const ImVec2 pad(16.0f, 14.0f);
    const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 popup_size(pad.x * 2.0f + item_size.x * 4.0f + gap * 3.0f,
        pad.y * 2.0f + item_size.y * 2.0f + 10.0f + spacing_y * 2.0f);
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 popup_pos(app.snap_layout_anchor.x - popup_size.x + 56.0f,
        app.snap_layout_anchor.y + 8.0f);
    popup_pos.x = std::clamp(popup_pos.x, viewport->Pos.x + 12.0f,
        viewport->Pos.x + viewport->Size.x - popup_size.x - 12.0f);
    popup_pos.y = std::clamp(popup_pos.y, viewport->Pos.y + 12.0f,
        viewport->Pos.y + viewport->Size.y - popup_size.y - 12.0f);

    ImGui::SetNextWindowPos(popup_pos);
    ImGui::SetNextWindowSize(popup_size);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    ImGui::PushStyleColor(ImGuiCol_Border, theme_chrome_line_color(app, 170));
    ImGui::Begin("izan-snap-layouts", nullptr, flags);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    bool used_layout = false;

    if (snap_layout_item(app, "##snap-left", item_size,
            { ImVec4(0.0f, 0.0f, 0.5f, 1.0f) })) {
        snap_window_to_work_area(window, 0.0f, 0.0f, 0.5f, 1.0f);
        used_layout = true;
    }
    ImGui::SameLine(0.0f, gap);
    if (snap_layout_item(app, "##snap-right", item_size,
            { ImVec4(0.5f, 0.0f, 1.0f, 1.0f) })) {
        snap_window_to_work_area(window, 0.5f, 0.0f, 0.5f, 1.0f);
        used_layout = true;
    }
    ImGui::SameLine(0.0f, gap);
    if (snap_layout_item(app, "##snap-full", item_size,
            { ImVec4(0.0f, 0.0f, 1.0f, 1.0f) })) {
        glfwMaximizeWindow(window);
        used_layout = true;
    }
    ImGui::SameLine(0.0f, gap);
    if (snap_layout_item(
            app, "##snap-top", item_size, { ImVec4(0.0f, 0.0f, 1.0f, 0.5f) })) {
        snap_window_to_work_area(window, 0.0f, 0.0f, 1.0f, 0.5f);
        used_layout = true;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    if (snap_layout_item(app, "##snap-left-two-thirds", item_size,
            { ImVec4(0.0f, 0.0f, 0.66f, 1.0f) })) {
        snap_window_to_work_area(window, 0.0f, 0.0f, 0.66f, 1.0f);
        used_layout = true;
    }
    ImGui::SameLine(0.0f, gap);
    if (snap_layout_item(app, "##snap-right-third", item_size,
            { ImVec4(0.66f, 0.0f, 1.0f, 1.0f) })) {
        snap_window_to_work_area(window, 0.66f, 0.0f, 0.34f, 1.0f);
        used_layout = true;
    }
    ImGui::SameLine(0.0f, gap);
    if (snap_layout_item(app, "##snap-bottom", item_size,
            { ImVec4(0.0f, 0.5f, 1.0f, 1.0f) })) {
        snap_window_to_work_area(window, 0.0f, 0.5f, 1.0f, 0.5f);
        used_layout = true;
    }
    ImGui::SameLine(0.0f, gap);
    if (snap_layout_item(app, "##snap-center", item_size,
            { ImVec4(0.18f, 0.12f, 0.82f, 0.88f) })) {
        snap_window_to_work_area(window, 0.15f, 0.10f, 0.70f, 0.80f);
        used_layout = true;
    }

    const bool popup_hovered
        = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (used_layout) {
        app.snap_layout_open = false;
        app.snap_layout_hover_started = -1.0;
        return;
    }
    const ImVec2 button_min(app.snap_layout_button_min.x - 10.0f,
        app.snap_layout_button_min.y - 10.0f);
    const ImVec2 button_max(app.snap_layout_button_max.x + 10.0f,
        app.snap_layout_button_max.y + 16.0f);
    const bool button_hovered
        = ImGui::IsMouseHoveringRect(button_min, button_max);
    if (popup_hovered || button_hovered) {
        app.snap_layout_last_hovered = ImGui::GetTime();
        return;
    }
    if (app.snap_layout_last_hovered < 0.0
        || ImGui::GetTime() - app.snap_layout_last_hovered > 0.20) {
        app.snap_layout_open = false;
        app.snap_layout_hover_started = -1.0;
        app.snap_layout_last_hovered = -1.0;
    }
}

void draw_custom_title_bar(GLFWwindow* window, ChromeState& app,
    const char* title_text, const char* subtitle_text)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 title_min(viewport->Pos.x + kWindowFrameMargin,
        viewport->Pos.y + kWindowFrameMargin);
    const ImVec2 title_size(
        viewport->Size.x - kWindowFrameMargin * 2.0f, kTitleBarHeight);
    const ImVec2 title_max(
        title_min.x + title_size.x, title_min.y + title_size.y);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float text_y = title_min.y
        + std::floor((kTitleBarHeight - ImGui::GetTextLineHeight()) * 0.5f);
    const float frame_height = ImGui::GetFrameHeight();
    const float frame_y
        = title_min.y + std::floor((kTitleBarHeight - frame_height) * 0.5f);
    const bool show_subtitle = title_size.x >= 620.0f;
    const float button_width = 46.0f;
    const float button_height = frame_height;
    const float slider_width = 112.0f;
    const float window_buttons_width
        = button_width * 3.0f + style.ItemSpacing.x * 2.0f;
    const float opacity_width = slider_width + style.ItemSpacing.x;
    const bool show_opacity = title_size.x >= 900.0f;
    const float controls_width
        = window_buttons_width + (show_opacity ? opacity_width : 0.0f);
    const float controls_x = title_max.x - controls_width - 10.0f;
    const ImVec2 empty_region {};
    const ImVec2 controls_min(controls_x - 8.0f, title_min.y);
    const ImVec2 controls_max(title_max.x, title_max.y);

    ImGui::SetNextWindowPos(title_min);
    ImGui::SetNextWindowSize(title_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    update_title_bar_hit_regions(window, title_min, title_max, empty_region,
        empty_region, controls_min, controls_max);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg, theme_transparent_chrome_window_color(app));
    ImGui::Begin("izan-titlebar", nullptr, flags);

    // The izan mark, by decree an emoji: one colored glyph from the
    // waterfall's emoji face, sitting ahead of the title.
    ImGui::PushFont(nullptr, 22.0f);
    const ImVec2 mark_size = ImGui::CalcTextSize("⛩️");
    ImGui::SetCursorScreenPos(ImVec2(title_min.x + 14.0f,
        title_min.y + std::floor((kTitleBarHeight - mark_size.y) * 0.5f)));
    ImGui::TextUnformatted("⛩️");
    ImGui::PopFont();
    const float title_x = title_min.x + 14.0f + mark_size.x + 6.0f;
    ImGui::SetCursorScreenPos(ImVec2(title_x, text_y));
    ImGui::TextUnformatted(title_text);
    if (show_subtitle && subtitle_text != nullptr) {
        // Subtitle follows the title's measured width; a fixed offset
        // overlaps once the title runs long.
        const float subtitle_x
            = title_x + ImGui::CalcTextSize(title_text).x + 24.0f;
        ImGui::SetCursorScreenPos(ImVec2(subtitle_x, text_y));
        ImGui::TextDisabled("%s", subtitle_text);
    }

    ImGui::SetCursorScreenPos(ImVec2(controls_x, frame_y));
    if (show_opacity) {
        int opacity_percent
            = static_cast<int>(std::lround(app.window_opacity * 100.0f));
        ImGui::SetNextItemWidth(slider_width);
        if (ImGui::SliderInt(
                "##window-opacity", &opacity_percent, 62, 100, "%d%%")) {
            app.window_opacity = static_cast<float>(opacity_percent) / 100.0f;
            glfwSetWindowOpacity(window, app.window_opacity);
        }
        ImGui::SameLine();
    }
    if (draw_window_control_button("##window-minimize",
            ImVec2(button_width, button_height), WindowControlIcon::Minimize))
        glfwIconifyWindow(window);
    update_title_bar_button_hit_region(window, WindowControlIcon::Minimize,
        ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        queue_simple_tooltip(app, "Minimize",
            ImVec2(ImGui::GetItemRectMin().x + button_width * 0.5f,
                ImGui::GetItemRectMax().y + 8.0f));
    ImGui::SameLine();
    const WindowControlIcon maximize_icon
        = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE
        ? WindowControlIcon::Restore
        : WindowControlIcon::Maximize;
    if (draw_window_control_button("##window-maximize",
            ImVec2(button_width, button_height), maximize_icon))
        toggle_window_maximized(window);
    const bool maximize_hovered
        = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    const ImVec2 maximize_min = ImGui::GetItemRectMin();
    const ImVec2 maximize_max = ImGui::GetItemRectMax();
    app.snap_layout_button_min = maximize_min;
    app.snap_layout_button_max = maximize_max;
    update_title_bar_button_hit_region(window, maximize_icon,
        ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (maximize_hovered) {
        app.snap_layout_last_hovered = ImGui::GetTime();
        if (app.snap_layout_hover_started < 0.0)
            app.snap_layout_hover_started = ImGui::GetTime();
        app.snap_layout_anchor
            = ImVec2((maximize_min.x + maximize_max.x) * 0.5f, maximize_max.y);
        if (ImGui::GetTime() - app.snap_layout_hover_started > 0.25) {
            app.snap_layout_open = true;
        } else if (!app.snap_layout_open) {
            queue_simple_tooltip(app,
                maximize_icon == WindowControlIcon::Restore ? "Restore"
                                                            : "Maximize",
                ImVec2(
                    app.snap_layout_anchor.x, app.snap_layout_anchor.y + 8.0f));
        }
    } else if (!app.snap_layout_open) {
        app.snap_layout_hover_started = -1.0;
        app.snap_layout_last_hovered = -1.0;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(196, 43, 59, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(150, 30, 46, 255));
    if (draw_window_control_button("##window-close",
            ImVec2(button_width, button_height), WindowControlIcon::Close))
        app.request_exit = true;
    update_title_bar_button_hit_region(window, WindowControlIcon::Close,
        ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        queue_simple_tooltip(app, "Close",
            ImVec2(ImGui::GetItemRectMin().x + button_width * 0.5f,
                ImGui::GetItemRectMax().y + 8.0f));
    ImGui::PopStyleColor(2);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void draw_custom_menu_bar(
    ChromeState& app, const std::function<void()>& draw_items)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 menu_min(viewport->Pos.x + kWindowFrameMargin,
        viewport->Pos.y + kWindowFrameMargin + kTitleBarHeight);
    const ImVec2 menu_size(
        viewport->Size.x - kWindowFrameMargin * 2.0f, kMenuBarHeight);

    ImGui::SetNextWindowPos(menu_min);
    ImGui::SetNextWindowSize(menu_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 0.0f));
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg, theme_chrome_background_color(app));
    ImGui::PushStyleColor(
        ImGuiCol_MenuBarBg, theme_chrome_background_color(app));
    ImGui::Begin("izan-menubar", nullptr, flags);

    if (ImGui::BeginMenuBar()) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 11.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
        ImVec4 popup_bg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        popup_bg.w = 1.0f;
        ImVec4 window_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        window_bg.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, popup_bg);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, window_bg);
        if (draw_items)
            draw_items();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::EndMenuBar();
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}

void draw_status_bar(const ChromeState& app, const char* status_text)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float height = kStatusBarHeight;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kWindowFrameMargin,
        viewport->WorkPos.y + viewport->WorkSize.y - kWindowFrameMargin
            - height));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x - kWindowFrameMargin * 2.0f, height));
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg, theme_chrome_background_color(app));
    ImGui::Begin("StatusBar", nullptr, flags);
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y);
    ImGui::GetWindowDrawList()->AddLine(
        min, max, theme_chrome_line_color(app, 150), 1.0f);
    ImGui::TextUnformatted(status_text);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

}
