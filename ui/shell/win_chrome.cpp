#include "ui/shell/win_chrome.hpp"

#include "ui/shell/constants.hpp"

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>

#include <dwmapi.h>
#include <windowsx.h>
#endif

namespace izan::ui {

#ifdef _WIN32
namespace {

    WNDPROC g_previous_window_proc = nullptr;
    RECT g_title_bar_rect {};
    RECT g_title_bar_menu_rect {};
    RECT g_title_bar_controls_rect {};
    RECT g_title_bar_minimize_rect {};
    RECT g_title_bar_maximize_rect {};
    RECT g_title_bar_close_rect {};
    bool g_title_bar_regions_valid = false;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

    bool is_window_maximized(HWND hwnd)
    {
        WINDOWPLACEMENT placement {};
        placement.length = sizeof(WINDOWPLACEMENT);
        return GetWindowPlacement(hwnd, &placement)
            && placement.showCmd == SW_MAXIMIZE;
    }

    bool point_in_win32_rect(const POINT& point, const RECT& rect)
    {
        return point.x >= rect.left && point.x < rect.right
            && point.y >= rect.top && point.y < rect.bottom;
    }

    bool point_in_title_bar_interactive_region(const POINT& point)
    {
        return point_in_win32_rect(point, g_title_bar_menu_rect)
            || point_in_win32_rect(point, g_title_bar_controls_rect);
    }

    bool point_in_fallback_title_controls(
        const POINT& point, const RECT& window_rect)
    {
        const LONG controls_left = window_rect.right - 390;
        const LONG title_bottom = window_rect.top + kWindowResizeBorder
            + static_cast<LONG>(kTitleBarHeight);
        return point.x >= controls_left && point.x < window_rect.right
            && point.y >= window_rect.top && point.y < title_bottom;
    }

    LRESULT CALLBACK custom_window_proc(
        HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message) {
        case WM_NCCALCSIZE:
            if (wparam == TRUE)
                return 0;
            break;
        case WM_NCHITTEST: {
            POINT point { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            RECT rect {};
            GetWindowRect(hwnd, &rect);

            const bool maximized = is_window_maximized(hwnd);

            if (!maximized) {
                const bool left = point.x >= rect.left
                    && point.x < rect.left + kWindowResizeBorder;
                const bool right = point.x <= rect.right
                    && point.x > rect.right - kWindowResizeBorder;
                const bool top = point.y >= rect.top
                    && point.y < rect.top + kWindowResizeBorder;
                const bool bottom = point.y <= rect.bottom
                    && point.y > rect.bottom - kWindowResizeBorder;

                if (left && top)
                    return HTTOPLEFT;
                if (right && top)
                    return HTTOPRIGHT;
                if (left && bottom)
                    return HTBOTTOMLEFT;
                if (right && bottom)
                    return HTBOTTOMRIGHT;
                if (left)
                    return HTLEFT;
                if (right)
                    return HTRIGHT;
                if (top)
                    return HTTOP;
                if (bottom)
                    return HTBOTTOM;
            }

            if (g_title_bar_regions_valid
                && point_in_win32_rect(point, g_title_bar_rect)) {
                return (point_in_title_bar_interactive_region(point)
                           || point_in_fallback_title_controls(point, rect))
                    ? HTCLIENT
                    : HTCAPTION;
            }
            if (point_in_fallback_title_controls(point, rect))
                return HTCLIENT;

            return HTCLIENT;
        }
        case WM_SETCURSOR: {
            // Custom cursor takeover (win_cursors): client area follows
            // ImGui's intent, borders follow the hit direction.
            if (!custom_cursors_active())
                break;
            const WORD hit = LOWORD(lparam);
            switch (hit) {
            case HTCLIENT:
                apply_custom_cursor();
                return TRUE;
            case HTCAPTION:
                apply_custom_cursor_slot(ImGuiMouseCursor_Arrow);
                return TRUE;
            case HTLEFT:
            case HTRIGHT:
                apply_custom_cursor_slot(ImGuiMouseCursor_ResizeEW);
                return TRUE;
            case HTTOP:
            case HTBOTTOM:
                apply_custom_cursor_slot(ImGuiMouseCursor_ResizeNS);
                return TRUE;
            case HTTOPLEFT:
            case HTBOTTOMRIGHT:
                apply_custom_cursor_slot(ImGuiMouseCursor_ResizeNWSE);
                return TRUE;
            case HTTOPRIGHT:
            case HTBOTTOMLEFT:
                apply_custom_cursor_slot(ImGuiMouseCursor_ResizeNESW);
                return TRUE;
            default:
                break;
            }
            break;
        }
        default:
            break;
        }

        return CallWindowProcW(
            g_previous_window_proc, hwnd, message, wparam, lparam);
    }

    RECT client_rect_to_screen(HWND hwnd, const ImVec2& min, const ImVec2& max)
    {
        POINT top_left { static_cast<LONG>(std::floor(min.x)),
            static_cast<LONG>(std::floor(min.y)) };
        POINT bottom_right { static_cast<LONG>(std::ceil(max.x)),
            static_cast<LONG>(std::ceil(max.y)) };
        ClientToScreen(hwnd, &top_left);
        ClientToScreen(hwnd, &bottom_right);
        return RECT { top_left.x, top_left.y, bottom_right.x, bottom_right.y };
    }

    RECT window_rect_for_visible_bounds(HWND hwnd, int visible_x, int visible_y,
        int visible_width, int visible_height)
    {
        RECT window_rect {};
        RECT frame_rect {};
        if (GetWindowRect(hwnd, &window_rect)
            && SUCCEEDED(
                DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                    &frame_rect, sizeof(frame_rect)))) {
            const int left_inset = frame_rect.left - window_rect.left;
            const int top_inset = frame_rect.top - window_rect.top;
            const int right_inset = window_rect.right - frame_rect.right;
            const int bottom_inset = window_rect.bottom - frame_rect.bottom;
            return RECT {
                visible_x - left_inset,
                visible_y - top_inset,
                visible_x + visible_width + right_inset,
                visible_y + visible_height + bottom_inset,
            };
        }
        return RECT { visible_x, visible_y, visible_x + visible_width,
            visible_y + visible_height };
    }

}

void install_custom_window_chrome(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr)
        return;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    const BOOL dark_mode = TRUE;
    DwmSetWindowAttribute(
        hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));

    MARGINS margins { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    g_previous_window_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(custom_window_proc)));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

void update_title_bar_hit_regions(GLFWwindow* window, const ImVec2& title_min,
    const ImVec2& title_max, const ImVec2& menu_min, const ImVec2& menu_max,
    const ImVec2& controls_min, const ImVec2& controls_max)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) {
        g_title_bar_regions_valid = false;
        return;
    }

    g_title_bar_rect = client_rect_to_screen(hwnd, title_min, title_max);
    g_title_bar_menu_rect = client_rect_to_screen(hwnd, menu_min, menu_max);
    g_title_bar_controls_rect
        = client_rect_to_screen(hwnd, controls_min, controls_max);
    g_title_bar_regions_valid = true;
}

void update_title_bar_button_hit_region(GLFWwindow* window,
    WindowControlIcon icon, const ImVec2& min, const ImVec2& max)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr)
        return;

    RECT rect = client_rect_to_screen(hwnd, min, max);
    switch (icon) {
    case WindowControlIcon::Minimize:
        g_title_bar_minimize_rect = rect;
        break;
    case WindowControlIcon::Maximize:
    case WindowControlIcon::Restore:
        g_title_bar_maximize_rect = rect;
        break;
    case WindowControlIcon::Close:
        g_title_bar_close_rect = rect;
        break;
    }
}

void set_window_screen_rect(GLFWwindow* window, int x, int y, int w, int h)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr)
        return;
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void set_window_visible_bounds(GLFWwindow* window, int x, int y, int w, int h)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr)
        return;
    const RECT target = window_rect_for_visible_bounds(hwnd, x, y, w, h);
    SetWindowPos(hwnd, nullptr, target.left, target.top,
        target.right - target.left, target.bottom - target.top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
#else
void install_custom_window_chrome(GLFWwindow*)
{
}

void set_window_screen_rect(GLFWwindow* window, int x, int y, int w, int h)
{
    glfwSetWindowPos(window, x, y);
    glfwSetWindowSize(window, w, h);
}

void set_window_visible_bounds(GLFWwindow* window, int x, int y, int w, int h)
{
    glfwSetWindowPos(window, x, y);
    glfwSetWindowSize(window, w, h);
}

void update_title_bar_hit_regions(GLFWwindow*, const ImVec2&, const ImVec2&,
    const ImVec2&, const ImVec2&, const ImVec2&, const ImVec2&)
{
}

void update_title_bar_button_hit_region(
    GLFWwindow*, WindowControlIcon, const ImVec2&, const ImVec2&)
{
}
#endif

WorkArea current_window_work_area(GLFWwindow* window)
{
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);
    HMONITOR win32_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info {};
    info.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(win32_monitor, &info)) {
        return WorkArea {
            info.rcWork.left,
            info.rcWork.top,
            info.rcWork.right - info.rcWork.left,
            info.rcWork.bottom - info.rcWork.top,
        };
    }
#endif
    GLFWmonitor* glfw_monitor = glfwGetWindowMonitor(window);
    if (glfw_monitor == nullptr)
        glfw_monitor = glfwGetPrimaryMonitor();
    int x = 0;
    int y = 0;
    int width = 1280;
    int height = 720;
    if (glfw_monitor != nullptr)
        glfwGetMonitorWorkarea(glfw_monitor, &x, &y, &width, &height);
    return WorkArea { x, y, width, height };
}

void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}
