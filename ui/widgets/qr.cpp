#include "ui/widgets/qr.hpp"

#include <string>

#include <imgui.h>
#include <qrcodegen.hpp>

namespace izan::ui {

void kit_qr(const char* text, float size_em)
{
    // One code shows at a time; a single-slot cache spares the
    // Reed-Solomon math from running every frame.
    static std::string cached_text;
    static qrcodegen::QrCode cached
        = qrcodegen::QrCode::encodeText("", qrcodegen::QrCode::Ecc::MEDIUM);
    if (cached_text != text) {
        cached = qrcodegen::QrCode::encodeText(
            text, qrcodegen::QrCode::Ecc::MEDIUM);
        cached_text = text;
    }

    const int n = cached.getSize();
    const int quiet = 3;
    const float want = ImGui::GetFontSize() * size_em;
    float cell = float(int(want / float(n + 2 * quiet)));
    if (cell < 1.0f)
        cell = 1.0f;
    const float side = cell * float(n + 2 * quiet);

    // The footprint is the ASKED size, always: a longer payload means
    // a denser code, never a bigger card — switching between codes of
    // different versions must not resize the window around them.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float inset_x = float(int((want - side) / 2));
    const float inset_y = inset_x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + want, pos.y + want),
        IM_COL32(255, 255, 255, 255), cell * 2.0f);
    const float ox = pos.x + inset_x + cell * float(quiet);
    const float oy = pos.y + inset_y + cell * float(quiet);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (cached.getModule(x, y))
                draw->AddRectFilled(
                    ImVec2(ox + cell * float(x), oy + cell * float(y)),
                    ImVec2(ox + cell * float(x + 1), oy + cell * float(y + 1)),
                    IM_COL32(10, 10, 12, 255));
    ImGui::Dummy(ImVec2(want, want));
}

}
