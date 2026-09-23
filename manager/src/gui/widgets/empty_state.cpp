#include "empty_state.h"
#include "../gui_scale.h"
#include "imgui.h"
#include "eam/widgets.h"

namespace eam {
namespace widgets {

void EmptyState(float width, const char* title, const char* hint) {
    ImGui::Dummy(ImVec2(0, S(24)));
    const float icoSize = ImGui::GetFontSize() * 3.0f;
    ImGui::SetCursorPosX((width - icoSize) * 0.5f);
    const ImVec2 ip = ImGui::GetCursorScreenPos();
    eam::ui::svg::Draw(ImGui::GetWindowDrawList(), eam::ui::icons::kPackage, ip, icoSize, eam::ui::theme::U(eam::ui::theme::kAccentDim), 1.6f);
    ImGui::Dummy(ImVec2(icoSize, icoSize));
    ImGui::SetCursorPosX((width - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PushTextWrapPos(width - S(12));
    ImGui::TextDisabled("%s", hint);
    ImGui::PopTextWrapPos();
}

} // namespace widgets
} // namespace eam
