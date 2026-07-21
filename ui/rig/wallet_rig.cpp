#include "ui/rig/wallet_rig.hpp"

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder: templates and adoption

namespace izan::ui {

WalletRig::WalletRig(const Paths& paths, const std::string& active_wallet)
    : vault_(paths.wallets_dir, paths.data_dir, paths.self_exe, active_wallet)
{
    // A broken chain/token config takes down one pane, not the wallet:
    // the vault stays reachable and the pane shows the error instead.
    try {
        portfolio_.emplace(paths.data_dir, paths.state_dir, vault_);
    } catch (const std::exception& e) {
        portfolio_error_ = e.what();
    }
    try {
        send_.emplace(paths.data_dir, paths.state_dir, vault_);
    } catch (const std::exception& e) {
        send_error_ = e.what();
    }
    try {
        swap_.emplace(paths.data_dir, paths.state_dir, vault_);
    } catch (const std::exception& e) {
        swap_error_ = e.what();
    }
    try {
        history_.emplace(paths.data_dir, vault_);
    } catch (const std::exception& e) {
        history_error_ = e.what();
    }

    // Touching a holding on the assets page walks it to the send form;
    // the row menu's swap verb walks it to the exchange desk.
    if (portfolio_ && send_)
        portfolio_->on_send([this](uint64_t chain_id, const std::string& sym,
                                const std::string& token, uint8_t decimals) {
            send_->prefill(chain_id, sym, token, decimals);
        });
    if (portfolio_ && swap_)
        portfolio_->on_swap([this](uint64_t chain_id, const std::string& sym) {
            swap_->prefill(chain_id, sym);
        });

    // A settled delivery anywhere staleness-marks the read-only pages;
    // their follow logic re-pulls balances and ledger next frame.
    const auto settled = [this] {
        if (portfolio_)
            portfolio_->mark_stale();
        if (history_)
            history_->mark_stale();
    };
    if (send_)
        send_->on_settled(settled);
    if (swap_)
        swap_->on_settled(settled);
}

namespace {

    // A dead pane still holds its dock slot: the error takes the
    // window the page would have had.
    void pane_error_window(
        const char* title, const char* id, const std::string& error)
    {
        ImGui::Begin((std::string(title) + id).c_str());
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::End();
    }

}

void WalletRig::draw(GLFWwindow* window, const i18n::Catalog& tr)
{
    vault_.draw(window, tr);
    if (send_)
        send_->draw(window, tr);
    else
        pane_error_window(tr("send.title"), "###send-page", send_error_);
    if (swap_)
        swap_->draw(window, tr);
    else
        pane_error_window(tr("swap.title"), "###swap-page", swap_error_);
    if (portfolio_)
        portfolio_->draw(tr);
    else
        pane_error_window(
            tr("portfolio.title"), "###portfolio-page", portfolio_error_);
    if (history_)
        history_->draw(tr);
    else
        pane_error_window(
            tr("history.title"), "###history-page", history_error_);
}

void wallet_dock_template(
    unsigned int dockspace_id, float width, float height, int tpl)
{
    const ImGuiID dockspace = dockspace_id;
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImVec2(width, height));
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
        ImGui::DockBuilderDockWindow("###swap-page", right_bottom);
    } else {
        ImGuiID left = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Left, 0.27f, nullptr, &center);
        const ImGuiID left_bottom = ImGui::DockBuilderSplitNode(
            left, ImGuiDir_Down, 0.48f, nullptr, &left);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Down, 0.40f, nullptr, &center);
        ImGui::DockBuilderDockWindow("###wallet-list", left);
        ImGui::DockBuilderDockWindow("###send-page", left_bottom);
        ImGui::DockBuilderDockWindow("###swap-page", left_bottom);
        ImGui::DockBuilderDockWindow("###vault-page", center);
        // Assets and the ledger share the bottom shelf as tabs.
        ImGui::DockBuilderDockWindow("###portfolio-page", bottom);
        ImGui::DockBuilderDockWindow("###history-page", bottom);
    }
    ImGui::DockBuilderFinish(dockspace);
}

void wallet_dock_adopt_saved()
{
    ImGuiWindow* ledger = ImGui::FindWindowByName("###history-page");
    ImGuiWindow* shelf = ImGui::FindWindowByName("###portfolio-page");
    if (ledger && shelf && ledger->DockId == 0 && shelf->DockId != 0)
        ImGui::DockBuilderDockWindow("###history-page", shelf->DockId);
    // Same adoption for the exchange desk: layouts saved before the
    // swap window existed float it — tuck it in beside the send page.
    ImGuiWindow* desk = ImGui::FindWindowByName("###swap-page");
    ImGuiWindow* teller = ImGui::FindWindowByName("###send-page");
    if (desk && teller && desk->DockId == 0 && teller->DockId != 0)
        ImGui::DockBuilderDockWindow("###swap-page", teller->DockId);
}

}
