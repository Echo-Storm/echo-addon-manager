#include "toast.h"
#include "../gui_scale.h"
#include "eam/widgets.h"
#include <deque>

namespace eam {
namespace widgets {

namespace {

struct Toast {
    std::string text;
    ToastType type;
    float lifetime;   // seconds it stays up
    float age;        // seconds it has been up
};

std::deque<Toast> g_toasts;
constexpr int kMostShown = 5;        // stacked at once, newest on top
constexpr size_t kMostKept = 20;     // waiting to be shown
constexpr float kFadeIn = 0.3f;
constexpr float kFadeOut = 0.5f;

struct Colours { ImVec4 fill, edge; };

Colours ColoursFor(ToastType type, float alpha) {
    using namespace eam::ui::theme;
    switch (type) {
        case ToastType::Success: return { ImVec4(0.243f, 0.353f, 0.180f, 0.95f * alpha), V(kAccent, alpha) };
        case ToastType::Warning: return { ImVec4(0.42f, 0.32f, 0.10f, 0.95f * alpha), V(kWarn, alpha) };
        case ToastType::Error:   return { ImVec4(0.290f, 0.125f, 0.125f, 0.95f * alpha), ImVec4(0.627f, 0.251f, 0.251f, alpha) };
        default:                 return { ImVec4(0.157f, 0.157f, 0.157f, 0.95f * alpha), V(kBorderBright, alpha) };
    }
}

float Opacity(const Toast& t) {
    float a = 1.0f;
    if (t.age < kFadeIn) a = t.age / kFadeIn;
    else if (t.age > t.lifetime - kFadeOut) a = (t.lifetime - t.age) / kFadeOut;
    return a < 0.0f ? 0.0f : a;
}

bool Expired(const Toast& t) { return t.age >= t.lifetime; }

} // namespace

void ToastShow(const std::string& message, ToastType type, float duration) {
    g_toasts.push_back({ message, type, duration, 0.0f });
    if (g_toasts.size() > kMostKept) g_toasts.pop_front();
}

void ToastRender() {
    const float dt = ImGui::GetIO().DeltaTime;
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    float top = S(10.0f);
    int shown = 0;
    for (int i = (int)g_toasts.size() - 1; i >= 0 && shown < kMostShown; --i) {
        Toast& t = g_toasts[i];
        t.age += dt;   // only the toasts that are on screen age
        if (Expired(t)) continue;

        const float alpha = Opacity(t);
        const Colours c = ColoursFor(t.type, alpha);

        const ImVec2 text = ImGui::CalcTextSize(t.text.c_str());
        const float pad = S(12.0f);
        const float w = text.x + pad * 2;
        const float h = text.y + pad * 2;
        const ImVec2 min(screen.x - w - S(15.0f), top), max(min.x + w, min.y + h);

        draw->AddRectFilled(min, max, ImGui::GetColorU32(c.fill), S(4.0f));
        draw->AddRect(min, max, ImGui::GetColorU32(c.edge), S(4.0f));
        draw->AddText(ImVec2(min.x + pad, min.y + pad), ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), t.text.c_str());

        top += h + S(5.0f);
        ++shown;
    }

    while (!g_toasts.empty() && Expired(g_toasts.front())) g_toasts.pop_front();
}

} // namespace widgets
} // namespace eam
