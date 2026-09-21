#include "toast.h"
#include "../gui_scale.h"
#include "lsproxy/lsp_widgets.h"
#include <deque>

namespace lsproxy {
namespace widgets {

struct ToastEntry {
    std::string message;
    ToastType type;
    float duration;
    float elapsed;
};

static std::deque<ToastEntry> s_toasts;
static constexpr int MAX_VISIBLE = 5;

void ToastShow(const std::string& message, ToastType type, float duration) {
    s_toasts.push_back({message, type, duration, 0.0f});
    if (s_toasts.size() > 20) s_toasts.pop_front();
}

void ToastRender() {
    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    float yOffset = S(10.0f);
    int visible = 0;

    for (int i = (int)s_toasts.size() - 1; i >= 0 && visible < MAX_VISIBLE; i--) {
        auto& t = s_toasts[i];
        t.elapsed += dt;

        if (t.elapsed >= t.duration) continue;

        // Fade in/out
        float alpha = 1.0f;
        if (t.elapsed < 0.3f) alpha = t.elapsed / 0.3f;
        else if (t.elapsed > t.duration - 0.5f) alpha = (t.duration - t.elapsed) / 0.5f;
        if (alpha < 0.0f) alpha = 0.0f;

        // Color based on type
        ImVec4 bgColor;
        switch (t.type) {
            case ToastType::Success: bgColor = ImVec4(0.243f, 0.353f, 0.180f, 0.95f * alpha); break;   // #3e5a2e
            case ToastType::Warning: bgColor = ImVec4(0.42f, 0.32f, 0.10f, 0.95f * alpha); break;
            case ToastType::Error:   bgColor = ImVec4(0.290f, 0.125f, 0.125f, 0.95f * alpha); break;   // #4a2020
            default:                 bgColor = ImVec4(0.157f, 0.157f, 0.157f, 0.95f * alpha); break;    // #282828
        }

        ImVec2 textSize = ImGui::CalcTextSize(t.message.c_str());
        float padding = S(12.0f);
        float toastWidth = textSize.x + padding * 2;
        float toastHeight = textSize.y + padding * 2;

        float x = displaySize.x - toastWidth - S(15.0f);
        float y = yOffset;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(
            ImVec2(x, y), ImVec2(x + toastWidth, y + toastHeight),
            ImGui::GetColorU32(bgColor), S(4.0f));
        drawList->AddRect(
            ImVec2(x, y), ImVec2(x + toastWidth, y + toastHeight),
            ImGui::GetColorU32(ImVec4(t.type == ToastType::Success ? lsp::theme::V(lsp::theme::kAccent, alpha)
                                     : t.type == ToastType::Warning ? lsp::theme::V(lsp::theme::kWarn, alpha)
                                     : t.type == ToastType::Error ? ImVec4(0.627f, 0.251f, 0.251f, alpha)
                                     : lsp::theme::V(lsp::theme::kBorderBright, alpha))), S(4.0f));
        drawList->AddText(
            ImVec2(x + padding, y + padding),
            ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)),
            t.message.c_str());

        yOffset += toastHeight + S(5.0f);
        visible++;
    }

    // Remove expired
    while (!s_toasts.empty() && s_toasts.front().elapsed >= s_toasts.front().duration) {
        s_toasts.pop_front();
    }
}

} // namespace widgets
} // namespace lsproxy
