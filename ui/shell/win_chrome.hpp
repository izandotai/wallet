#pragma once

#include <imgui.h>

#include <filesystem>

struct GLFWwindow;

namespace izan::ui {

// Borderless self-drawn window frame on Win32: WS_THICKFRAME keeps
// system drag/snap alive, DWM paints the dark caption, and a custom
// WM_NCHITTEST maps the drawn title bar to caption/resize behavior.
// Everything is a no-op off Windows.

enum class WindowControlIcon {
    Minimize,
    Maximize,
    Restore,
    Close,
};

struct WorkArea {
    int x = 0;
    int y = 0;
    int width = 1280;
    int height = 720;
};

void install_custom_window_chrome(GLFWwindow* window);
// Places the window by raw screen rect. With the custom chrome the
// client area IS the window rect; glfwSetWindowPos/Size would pad the
// numbers by decoration insets this window no longer has, drifting the
// frame a border's width per launch.
void set_window_screen_rect(GLFWwindow* window, int x, int y, int w, int h);
// Places the window so its VISIBLE bounds (DWM extended frame) land on
// the given rect — the primitive under snapping and centering, where
// the target is a slice of the work area and any invisible frame edge
// must hang outside it.
void set_window_visible_bounds(GLFWwindow* window, int x, int y, int w, int h);
void update_title_bar_hit_regions(GLFWwindow* window, const ImVec2& title_min,
    const ImVec2& title_max, const ImVec2& menu_min, const ImVec2& menu_max,
    const ImVec2& controls_min, const ImVec2& controls_max);
void update_title_bar_button_hit_region(GLFWwindow* window,
    WindowControlIcon icon, const ImVec2& min, const ImVec2& max);

WorkArea current_window_work_area(GLFWwindow* window);

void glfw_error_callback(int error, const char* description);

// Custom mouse cursors (win_cursors.cpp): loads a .cur/.ani set from a
// directory and takes over cursor shape via WM_SETCURSOR plus a
// per-frame fallback. Missing directory or missing Arrow returns false
// and the system cursors stay; the registry is never touched.
bool install_custom_cursors(const std::filesystem::path& dir);
bool custom_cursors_active();
void apply_custom_cursor();                      // by ImGui::GetMouseCursor()
void apply_custom_cursor_slot(int imgui_cursor); // explicit slot (frame)

}
