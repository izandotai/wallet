include(FetchContent)

# UI dependency set: GLFW window/input, FreeType glyph rasterizing (CJK
# and emoji need it — stb_truetype hinting is not good enough), Dear
# ImGui docking branch. All release tarballs, same pinning discipline
# as the rest of the tree. ImPlot joins in v1.5 with the portfolio
# chart, not before.

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8-docking.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glfw freetype imgui)

find_package(OpenGL REQUIRED)

add_library(izan_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
)
target_include_directories(izan_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/freetype
)
# WCHAR32 so emoji outside the BMP survive the text pipeline.
target_compile_definitions(izan_imgui PUBLIC
    IMGUI_ENABLE_FREETYPE IMGUI_USE_WCHAR32)
target_link_libraries(izan_imgui PUBLIC glfw freetype OpenGL::GL)
