#include "empty_state.h"
#include "../gui_scale.h"
#include "imgui.h"
#include "lsproxy/lsp_widgets.h"

namespace lsproxy {
namespace widgets {

void EmptyState(float width, const char* title, const char* hint) {
    ImGui::Dummy(ImVec2(0, S(24)));
    const float icoSize = ImGui::GetFontSize() * 3.0f;
    ImGui::SetCursorPosX((width - icoSize) * 0.5f);
    const ImVec2 ip = ImGui::GetCursorScreenPos();
    lsp::svg::Draw(ImGui::GetWindowDrawList(), lsp::icons::kPackage, ip, icoSize, lsp::theme::U(lsp::theme::kAccentDim), 1.6f);
    ImGui::Dummy(ImVec2(icoSize, icoSize));
    ImGui::SetCursorPosX((width - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PushTextWrapPos(width - S(12));
    ImGui::TextDisabled("%s", hint);
    ImGui::PopTextWrapPos();
}

} // namespace widgets
} // namespace lsproxy
