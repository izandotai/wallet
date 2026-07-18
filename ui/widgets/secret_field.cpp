#include "ui/widgets/secret_field.hpp"

#include <cstring>

#include <imgui.h>
#include <sodium.h>

namespace izan::ui {

void secret_field(
    const char* label, std::array<char, 256>& buf, bool& secret_focus)
{
    ImGui::InputText(label, buf.data(), buf.size(),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_AutoSelectAll);
    secret_focus |= ImGui::IsItemActive();
}

secure::SecureBytes take_secret(std::array<char, 256>& buf)
{
    const std::size_t len = strnlen(buf.data(), buf.size());
    secure::SecureBytes out(len);
    if (len)
        std::memcpy(out.data(), buf.data(), len);
    sodium_memzero(buf.data(), buf.size());
    return out;
}

}
