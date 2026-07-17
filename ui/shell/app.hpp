#pragma once

#include <imgui.h>

#include <functional>

struct GLFWwindow;

namespace izan::ui {

// GLFW + ImGui (docking, freetype) application lifecycle:
// init / begin_frame / end_frame / shutdown. imgui's own ini file is
// disabled; layout persistence is the host's business.

struct AppOptions {
    const char* title = "izan";
    int width = 1280;
    int height = 800;
    bool attach_parent_console = true;
};

class GlfwApp {
public:
    GlfwApp() = default;
    ~GlfwApp();

    GlfwApp(const GlfwApp&) = delete;
    GlfwApp& operator=(const GlfwApp&) = delete;

    // The window is created hidden and shown after the first rendered
    // frame — no white flash on startup.
    bool init(const AppOptions& options);
    void show();
    void shutdown();

    GLFWwindow* window() const
    {
        return window_;
    }

    bool should_close() const;
    void poll_events() const;

    void begin_frame() const;
    void end_frame(const ImVec4& clear_color) const;

    // Registering the whole-frame render function also hooks it up as
    // the window refresh callback: Windows traps glfwPollEvents inside
    // the modal size loop while a border is dragged, so every repaint
    // during the drag comes from that callback — skip it and resizing
    // smears the frame. run() = first frame → show() → poll+render
    // until should_close.
    void set_render_callback(std::function<void()> render);
    void run();

private:
    GLFWwindow* window_ = nullptr;
    bool initialized_ = false;
    std::function<void()> render_;
};

}
