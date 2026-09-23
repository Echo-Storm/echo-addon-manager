#pragma once
// Shared look for Echo Addon Manager and its addons: the dark neutral + green scheme used by TorBox Manager and Echo Audio
// Converter (bg #181818, panel #1f1f1f, accent #7cb342), plus a slider and a section label that match it.
//
// Header only and ABI-free: it just calls ImGui, so an addon can include it and get the same widgets as the manager
// without a new host interface. Include it after imgui.h. The manager applies the colours through the ImGui style
// (gui_style.cpp); this file adds what the style cannot express.
#include "imgui.h"
#include "icons.h"
#include "imgui_internal.h"   // ImGui::GetActiveID: tells a slider whose value is being typed from one that is only shown
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

// Defined by ImGui (imgui_draw.cpp) in versions that have text classifiers; declared here so addons need not include
// imgui_internal.h.
void ImTextInitClassifiers();

namespace eam::ui {

// Call from AddonInitialize right after ImGui::SetCurrentContext / SetAllocatorFunctions. Each addon carries its own copy of
// ImGui, and the word-wrap character classes are file-scope tables that ImGui only fills when it builds a font atlas (in the
// host's copy). Without this, TextWrapped in the addon cuts words in half.
inline void InitAddonImGui() {
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19290
    ImTextInitClassifiers();
#endif
}

namespace theme {

// The scheme, as 0..1 floats (ImGui's ImVec4) so it can be used for style colours and for draw-list colours alike.
constexpr float kBg[3]         = { 0.094f, 0.094f, 0.094f };   // #181818 window
constexpr float kPanel[3]      = { 0.122f, 0.122f, 0.122f };   // #1f1f1f panels / child windows
constexpr float kPanelAlt[3]   = { 0.137f, 0.137f, 0.137f };   // #232323 popups, alternate rows
constexpr float kAccent[3]     = { 0.486f, 0.702f, 0.259f };   // #7cb342 primary accent
constexpr float kAccentDim[3]  = { 0.290f, 0.420f, 0.157f };   // #4a6b28 dimmed accent
constexpr float kAccentHot[3]  = { 0.612f, 0.800f, 0.396f };   // #9ccc65 hover / active accent
constexpr float kSelection[3]  = { 0.243f, 0.353f, 0.180f };   // #3e5a2e selection (Echo Audio Converter)
constexpr float kRowHover[3]   = { 0.145f, 0.184f, 0.102f };   // #252f1a very subtle green tint
constexpr float kText[3]       = { 0.910f, 0.910f, 0.910f };   // #e8e8e8
constexpr float kMuted[3]      = { 0.400f, 0.400f, 0.400f };   // #666666
constexpr float kButton[3]     = { 0.157f, 0.157f, 0.157f };   // #282828
constexpr float kButtonHover[3]= { 0.196f, 0.196f, 0.196f };   // #323232
constexpr float kBorder[3]     = { 0.165f, 0.165f, 0.165f };   // #2a2a2a
constexpr float kBorderBright[3]={ 0.227f, 0.227f, 0.227f };   // #3a3a3a
constexpr float kDanger[3]     = { 0.816f, 0.502f, 0.502f };   // #d08080 text on the red buttons
constexpr float kWarn[3]       = { 0.941f, 0.741f, 0.259f };   // amber for cautions
constexpr float kKofi[3]       = { 1.000f, 0.369f, 0.357f };   // #ff5e5b Ko-fi red (the heart on the donate button)

inline ImVec4 V(const float c[3], float a = 1.0f) { return ImVec4(c[0], c[1], c[2], a); }
inline ImU32 U(const float c[3], float a = 1.0f) { return ImGui::ColorConvertFloat4ToU32(V(c, a)); }


// The whole scheme as an ImGui style (rounding, spacing, colours). The manager calls this from its SetupModernStyle;
// a test host or preview can call it to look like the manager. Only touches the style it is given.
inline void ApplyStyle(ImGuiStyle& style) {


    // Tighter, squarer corners than before: the same 3 to 4 px radius as TorBox Manager / Echo Audio Converter.
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.ChildRounding = 4.0f;

    // Spacing
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.ScrollbarSize = 11.0f;

    // Border
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    ImVec4* colors = style.Colors;

    // Window
    colors[ImGuiCol_WindowBg]        = V(kBg);
    colors[ImGuiCol_ChildBg]         = V(kPanel);
    colors[ImGuiCol_PopupBg]         = V(kPanelAlt);
    colors[ImGuiCol_Border]          = V(kBorder);
    colors[ImGuiCol_BorderShadow]    = ImVec4(0, 0, 0, 0);

    // Title
    colors[ImGuiCol_TitleBg]         = V(kBg);
    colors[ImGuiCol_TitleBgActive]   = V(kBg);
    colors[ImGuiCol_TitleBgCollapsed]= V(kBg);

    // Menu / Header (selectables, collapsing headers, list rows)
    colors[ImGuiCol_MenuBarBg]       = V(kPanel);
    colors[ImGuiCol_Header]          = V(kSelection, 0.55f);
    colors[ImGuiCol_HeaderHovered]   = V(kRowHover);
    colors[ImGuiCol_HeaderActive]    = V(kSelection);

    // Button (neutral, green on hover like the reference apps)
    colors[ImGuiCol_Button]          = V(kButton);
    colors[ImGuiCol_ButtonHovered]   = V(kButtonHover);
    colors[ImGuiCol_ButtonActive]    = V(kSelection);

    // Frame (inputs, slider tracks): #252525 with a #3a3a3a border
    colors[ImGuiCol_FrameBg]         = ImVec4(0.145f, 0.145f, 0.145f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.170f, 0.170f, 0.170f, 1.0f);
    colors[ImGuiCol_FrameBgActive]   = ImVec4(0.190f, 0.190f, 0.190f, 1.0f);

    // Tabs: dim text-on-dark with a green selected state (the reference apps underline the selected tab in green)
    colors[ImGuiCol_Tab]                = V(kBg);
    colors[ImGuiCol_TabHovered]         = V(kPanel);
    colors[ImGuiCol_TabSelected]        = V(kPanel);
    colors[ImGuiCol_TabSelectedOverline]= V(kAccent);
    colors[ImGuiCol_TabDimmed]          = V(kBg);
    colors[ImGuiCol_TabDimmedSelected]  = V(kPanel);
    colors[ImGuiCol_TabDimmedSelectedOverline] = V(kAccentDim);

    // Accent elements
    colors[ImGuiCol_CheckMark]       = V(kAccent);
    colors[ImGuiCol_SliderGrab]      = V(kAccent);
    colors[ImGuiCol_SliderGrabActive]= V(kAccentHot);
    colors[ImGuiCol_TextSelectedBg]  = V(kSelection, 0.8f);
    colors[ImGuiCol_NavCursor]       = V(kAccent);
    colors[ImGuiCol_CheckboxSelectedBg] = V(kSelection);
    colors[ImGuiCol_InputTextCursor] = V(kText);
    colors[ImGuiCol_TextLink]        = V(kAccentHot);
    colors[ImGuiCol_DragDropTarget]  = V(kAccent);
    colors[ImGuiCol_TableHeaderBg]   = V(kPanelAlt);
    colors[ImGuiCol_TableBorderStrong] = V(kBorderBright);
    colors[ImGuiCol_TableBorderLight]  = V(kBorder);
    colors[ImGuiCol_DockingPreview]  = V(kAccentDim, 0.6f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]     = V(kBg);
    colors[ImGuiCol_ScrollbarGrab]   = V(kBorderBright);
    colors[ImGuiCol_ScrollbarGrabHovered] = V(kAccentDim);
    colors[ImGuiCol_ScrollbarGrabActive]  = V(kAccent);

    // Separator
    colors[ImGuiCol_Separator]       = V(kBorder);
    colors[ImGuiCol_SeparatorHovered]= V(kAccentDim);
    colors[ImGuiCol_SeparatorActive] = V(kAccent);

    // Resize
    colors[ImGuiCol_ResizeGrip]        = V(kBorderBright, 0.5f);
    colors[ImGuiCol_ResizeGripHovered] = V(kAccentDim);
    colors[ImGuiCol_ResizeGripActive]  = V(kAccent);

    // Text
    colors[ImGuiCol_Text]            = V(kText);
    colors[ImGuiCol_TextDisabled]    = V(kMuted);
}

} // namespace theme

// A small uppercase label, letter-spaced, in the accent: the "ADD" / "QUEUE" / "LOG" headings of the other apps. It is the heading of a block
// that cannot be closed, in the same green as the title of an open SectionHeader, so an open section and a plain heading read alike.
inline void SectionLabel(const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float spacing = (std::max)(1.5f, ImGui::GetFontSize() * 0.16f);
    float x = p.x;
    char up[2] = { 0, 0 };
    for (const char* c = text; *c; ++c) {
        up[0] = (char)toupper((unsigned char)*c);
        dl->AddText(ImVec2(x, p.y), theme::U(theme::kAccent, ImGui::GetStyle().Alpha), up);
        x += ImGui::CalcTextSize(up).x + spacing;
    }
    ImGui::Dummy(ImVec2(x - p.x, ImGui::GetTextLineHeight()));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Sliders
// ---------------------------------------------------------------------------------------------------------------------------------
// Thin groove, filled with the accent up to the knob, value printed above it. Drop-in for ImGui::SliderFloat / SliderInt (same
// arguments; the label sits to the right as usual). The stock slider still does the input, drawn with transparent colours and
// submitted first, so dragging, Ctrl+click typing and keyboard navigation all keep working and the look is drawn from the finished
// item state (hover, drag, value) in the same frame. While a value is being typed the stock widget draws itself, so the typed text
// stays visible.
//
// Extra behaviour when a default value is passed (the last argument):
//   * a small tick on the groove marks the default, and the knob gets a ring while the value differs from it;
//   * double-click resets the value to the default;
// and always:
//   * Ctrl + mouse wheel over the slider nudges the value (1% of the range per notch, Shift = 0.1%; whole steps for integers);
//   * eam::ui::SliderHint() returns the one-line "how to use it" text for the slider just submitted, so a tooltip helper can append it.
namespace detail {

struct HintSlot { ImGuiID id = 0; char text[160] = {}; };
inline HintSlot& Hint() { static HintSlot h; return h; }

inline void DrawSlider(ImVec2 p, float w, float h, float t, const char* valueText, bool hovered, bool active, bool hasDef, float defT, bool modified) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float a = ImGui::GetStyle().Alpha;   // follows BeginDisabled and fades
    const float grooveH = (std::max)(3.0f, h * 0.14f);
    const float pad = h * 0.30f;
    const float x0 = p.x + pad, x1 = p.x + w - pad;
    const float cy = p.y + h - pad * 0.95f;
    const float kx = x0 + (x1 - x0) * (std::min)(1.0f, (std::max)(0.0f, t));
    const float r = (std::max)(4.0f, h * 0.20f);
    dl->AddRectFilled(ImVec2(x0, cy - grooveH * 0.5f), ImVec2(x1, cy + grooveH * 0.5f), theme::U(theme::kBorderBright, a), grooveH * 0.5f);
    if (kx > x0) dl->AddRectFilled(ImVec2(x0, cy - grooveH * 0.5f), ImVec2(kx, cy + grooveH * 0.5f), theme::U(theme::kAccentDim, a), grooveH * 0.5f);
    if (hasDef) {   // the default: a short tick across the groove
        const float dx = x0 + (x1 - x0) * (std::min)(1.0f, (std::max)(0.0f, defT));
        dl->AddLine(ImVec2(dx, cy - grooveH * 1.7f), ImVec2(dx, cy + grooveH * 1.7f), theme::U(theme::kMuted, a), 1.0f);
    }
    dl->AddCircleFilled(ImVec2(kx, cy), r, theme::U(hovered || active ? theme::kAccentHot : theme::kAccent, a), 20);
    dl->AddCircle(ImVec2(kx, cy), r, theme::U(theme::kBg, a), 20, 1.0f);
    if (modified) dl->AddCircle(ImVec2(kx, cy), r + 1.5f, theme::U(theme::kAccentHot, a * 0.55f), 20, 1.0f);   // ring: changed from the default
    const ImVec2 ts = ImGui::CalcTextSize(valueText);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - pad * 1.9f - ts.y) * 0.5f + h * 0.02f), theme::U(theme::kText, a), valueText);
}

// `stock` runs the real ImGui slider (with an empty format) and returns whether it changed; `value(buf, n)` writes the text to show
// and returns the knob position 0..1 (called after the stock slider, so it sees the new value); `isDefault()` says whether the value
// equals the default; `reset()` puts the default back; `nudge(notches)` moves the value by mouse-wheel notches.
template <class Stock, class Value, class IsDefault, class Reset, class Nudge>
inline bool Slider(const char* label, Stock stock, Value value, bool hasDef, float defT, const char* defText, IsDefault isDefault, Reset reset, Nudge nudge) {
    const float w = ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();
    const ImGuiID id = ImGui::GetID(label);
    const bool typing = ImGui::GetActiveID() == id && ImGui::GetIO().WantTextInput;
    ImGui::SetNextItemWidth(w);
    if (!typing) {
        const ImVec4 none(0, 0, 0, 0);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, none); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, none);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, none); ImGui::PushStyleColor(ImGuiCol_SliderGrab, none);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, none); ImGui::PushStyleColor(ImGuiCol_Border, none);
    }
    bool changed = stock();
    if (!typing) ImGui::PopStyleColor(6);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (!typing && hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        if (hasDef && ImGui::IsMouseDoubleClicked(0)) { reset(); changed = true; }
        else if (io.KeyCtrl && io.MouseWheel != 0.0f) { nudge(io.MouseWheel * (io.KeyShift ? 0.1f : 1.0f)); changed = true; }
    }
    if (!typing) {
        char text[64]; const float t = value(text, sizeof text);
        DrawSlider(ImGui::GetItemRectMin(), w, h, t, text, hovered, active, hasDef, defT, hasDef && !isDefault());
    }
    HintSlot& hs = Hint();
    hs.id = ImGui::GetItemID();
    if (hasDef) snprintf(hs.text, sizeof hs.text, "Default: %s. Double-click resets it. Ctrl+click types a value. Ctrl+scroll fine-tunes.", defText);
    else snprintf(hs.text, sizeof hs.text, "Ctrl+click types a value. Ctrl+scroll fine-tunes.");
    return changed;
}
} // namespace detail

// The "how to use it" line for the slider submitted just before (or nullptr if the last item was not one of ours). A tooltip helper
// can append it to whatever it shows.
inline const char* SliderHint() {
    const detail::HintSlot& h = detail::Hint();
    return (h.id != 0 && h.id == ImGui::GetItemID()) ? h.text : nullptr;
}

inline bool SliderFloat(const char* label, float* v, float vmin, float vmax, const char* format = "%.2f", ImGuiSliderFlags flags = 0, const float* defaultValue = nullptr) {
    const bool hasDef = defaultValue != nullptr;
    const float dv = hasDef ? *defaultValue : 0.0f;
    char defText[64] = ""; if (hasDef) snprintf(defText, sizeof defText, format, dv);
    const float range = vmax - vmin;
    return detail::Slider(label,
        [&] { return ImGui::SliderFloat(label, v, vmin, vmax, "", flags); },
        [&](char* buf, size_t n) { snprintf(buf, n, format, *v); return range != 0 ? (*v - vmin) / range : 0.0f; },
        hasDef, range != 0 ? (dv - vmin) / range : 0.0f, defText,
        [&] { return fabsf(*v - dv) <= 1e-4f * (std::max)(1.0f, fabsf(range)); },
        [&] { *v = dv; },
        [&](float notches) { *v = (std::min)(vmax, (std::max)(vmin, *v + notches * 0.01f * range)); });
}

inline bool SliderInt(const char* label, int* v, int vmin, int vmax, const char* format = "%d", ImGuiSliderFlags flags = 0, const int* defaultValue = nullptr) {
    const bool hasDef = defaultValue != nullptr;
    const int dv = hasDef ? *defaultValue : 0;
    char defText[64] = ""; if (hasDef) snprintf(defText, sizeof defText, format, dv);
    const float range = (float)(vmax - vmin);
    return detail::Slider(label,
        [&] { return ImGui::SliderInt(label, v, vmin, vmax, "", flags); },
        [&](char* buf, size_t n) { snprintf(buf, n, format, *v); return range != 0 ? (float)(*v - vmin) / range : 0.0f; },
        hasDef, range != 0 ? (float)(dv - vmin) / range : 0.0f, defText,
        [&] { return *v == dv; },
        [&] { *v = dv; },
        [&](float notches) { const int step = notches > 0 ? 1 : -1; *v = (std::min)(vmax, (std::max)(vmin, *v + step)); });
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Section headers, buttons, icons
// ---------------------------------------------------------------------------------------------------------------------------------

// A collapsible section: a boxed plus or minus, letter-spaced uppercase label in the accent, a hairline under it. Returns whether it is open
// (state is kept by ImGui per window, keyed by the label, like ImGui::CollapsingHeader; `defaultOpen` applies the first time).
inline bool SectionHeader(const char* label, bool defaultOpen = false) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;
    const ImGuiID stateId = ImGui::GetID(label);
    ImGuiStorage* st = ImGui::GetStateStorage();
    int open = st->GetInt(stateId, defaultOpen ? 1 : 0);
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() * 1.05f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##hdr", ImVec2(w, h));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    if (pressed) { open = !open; st->SetInt(stateId, open); }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float a = ImGui::GetStyle().Alpha;
    if (hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::U(theme::kRowHover, a), 3.0f);
    // The open / close control: a small box with a plus (closed) or a minus (open), so it reads as a button. White when closed, green when open or hovered.
    const float box = ImGui::GetFontSize() * 0.95f;
    const float bx = p.x + h * 0.15f, by = p.y + (h - box) * 0.5f;
    const ImU32 sign = theme::U(open ? theme::kAccent : (hovered ? theme::kAccentHot : theme::kText), a);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + box, by + box), theme::U(hovered ? theme::kButtonHover : theme::kButton, a), 3.0f);
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + box, by + box), theme::U(open || hovered ? theme::kAccentDim : theme::kBorderBright, a), 3.0f, 0, 1.2f);
    const float cx = bx + box * 0.5f, cy = by + box * 0.5f, arm = box * 0.27f, thick = (std::max)(1.6f, ImGui::GetFontSize() * 0.12f);
    dl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), sign, thick);
    if (!open) dl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), sign, thick);
    const float spacing = (std::max)(1.2f, ImGui::GetFontSize() * 0.12f);
    float x = bx + box + ImGui::GetFontSize() * 0.65f;
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    char up[2] = { 0, 0 };
    const ImU32 col = theme::U(open || hovered ? theme::kAccent : theme::kAccentDim, a);
    for (const char* c = label; *c; ++c) {
        up[0] = (char)toupper((unsigned char)*c);
        if (x + ImGui::CalcTextSize(up).x > p.x + w - 4.0f) break;   // clip rather than spill out of the row
        dl->AddText(ImVec2(x, ty), col, up);
        x += ImGui::CalcTextSize(up).x + spacing;
    }
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + w, p.y + h), theme::U(theme::kBorder, a), 1.0f);
    return open != 0;
}

enum class ButtonKind { Normal, Primary, Danger, Flat };

// A button with an optional SVG icon in front of the label (icon may be nullptr). Primary is the green button of the other apps
// (#2e4a1e, green border and text), Danger the red one, Flat has no frame (a link-like control).
inline bool Button(const char* label, const char* icon = nullptr, ButtonKind kind = ButtonKind::Normal, ImVec2 size = ImVec2(0, 0)) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* labelEnd = strstr(label, "##");
    const ImVec2 ts = ImGui::CalcTextSize(label, labelEnd);
    const float ico = icon ? ImGui::GetFontSize() * 0.95f : 0.0f, gap = icon && ts.x > 0 ? ImGui::GetFontSize() * 0.45f : 0.0f;
    const float padX = kind == ButtonKind::Flat ? style.FramePadding.x * 0.5f : style.FramePadding.x * 1.25f;
    ImVec2 sz(size.x > 0 ? size.x : ts.x + ico + gap + padX * 2, size.y > 0 ? size.y : ImGui::GetFrameHeight());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, sz);
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float a = style.Alpha;
    ImU32 bg, border, fg;
    using namespace theme;
    if (kind == ButtonKind::Primary) {
        const float pb[3] = { 0.180f, 0.290f, 0.118f };   // #2e4a1e
        bg = U(hovered || held ? kSelection : pb, a); border = U(kAccent, a); fg = U(kAccentHot, a);
    } else if (kind == ButtonKind::Danger) {
        const float db[3] = { 0.290f, 0.125f, 0.125f };   // #4a2020
        const float dh[3] = { 0.36f, 0.16f, 0.16f };
        const float dbd[3] = { 0.627f, 0.251f, 0.251f };  // #a04040
        bg = U(hovered || held ? dh : db, a); border = U(dbd, a); fg = U(kDanger, a);
    } else if (kind == ButtonKind::Flat) {
        bg = hovered ? U(kRowHover, a) : 0; border = 0; fg = U(hovered ? kAccent : kMuted, a);
    } else {
        bg = U(held ? kSelection : hovered ? kButtonHover : kButton, a); border = U(hovered ? kAccentDim : kBorderBright, a); fg = U(kText, a);
    }
    if (bg) dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), bg, style.FrameRounding);
    if (border) dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), border, style.FrameRounding, 0, 1.0f);
    const float contentW = ico + gap + ts.x;
    float x = p.x + (sz.x - contentW) * 0.5f;
    if (icon) { svg::Draw(dl, icon, ImVec2(x, p.y + (sz.y - ico) * 0.5f), ico, fg, 2.0f); x += ico + gap; }
    if (ts.x > 0) dl->AddText(ImVec2(x, p.y + (sz.y - ts.y) * 0.5f), fg, label, labelEnd);
    return pressed;
}

// A square, frameless icon button (tooltip is the caller's job: ImGui::SetItemTooltip after it).
inline bool IconButton(const char* id, const char* icon, float size = 0.0f, ImU32 colour = 0) {
    const float s = size > 0 ? size : ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(s, s));
    const bool hovered = ImGui::IsItemHovered();
    const float a = ImGui::GetStyle().Alpha;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), theme::U(theme::kRowHover, a), 3.0f);
    const float ico = s * 0.58f;
    svg::Draw(dl, icon, ImVec2(p.x + (s - ico) * 0.5f, p.y + (s - ico) * 0.5f), ico, colour ? colour : theme::U(hovered ? theme::kAccent : theme::kMuted, a), 2.0f);
    return pressed;
}

// A line graph on a dark panel. `pts` are (x, y) with x = 0..1 across the graph (time, oldest at 0) and y in the data's own units;
// the y axis runs ymin..ymax. `guides` are horizontal reference lines (with optional labels drawn at the right end). The line is
// drawn in `line`, and in `warn` wherever it is above `warnAbove` (pass warnAbove <= 0 for no colour change). Hovering shows the
// value under the pointer. Returns nothing; the widget takes `size` (a zero width means "the rest of the row").
inline void LineGraph(const char* id, const ImVec2* pts, int n, ImVec2 size, float ymin, float ymax, ImU32 line, const float* guides = nullptr,
                      const char* const* guideLabels = nullptr, int nGuides = 0, float warnAbove = 0.0f, ImU32 warn = 0, const char* unit = "") {
    ImGui::PushID(id);
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##graph", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float a = ImGui::GetStyle().Alpha;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), theme::U(theme::kPanel, a), 4.0f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), theme::U(theme::kBorder, a), 4.0f, 0, 1.0f);
    const float pad = 6.0f;
    const ImVec2 lo(p.x + pad, p.y + pad), hi(p.x + size.x - pad, p.y + size.y - pad);
    const float rng = (ymax > ymin) ? (ymax - ymin) : 1.0f;
    auto Y = [&](float v) { const float t = (v - ymin) / rng; return hi.y - (t < 0 ? 0 : (t > 1 ? 1 : t)) * (hi.y - lo.y); };
    for (int g = 0; g < nGuides; ++g) {   // dashed reference lines
        const float y = Y(guides[g]);
        for (float x = lo.x; x < hi.x; x += 8.0f) dl->AddLine(ImVec2(x, y), ImVec2((std::min)(x + 4.0f, hi.x), y), theme::U(theme::kBorderBright, a), 1.0f);
        if (guideLabels && guideLabels[g]) {
            const ImVec2 ts = ImGui::CalcTextSize(guideLabels[g]);
            dl->AddText(ImVec2(hi.x - ts.x, y - ts.y - 1.0f), theme::U(theme::kMuted, a), guideLabels[g]);
        }
    }
    for (int i = 1; i < n; ++i) {
        const ImVec2 a0(lo.x + pts[i - 1].x * (hi.x - lo.x), Y(pts[i - 1].y)), a1(lo.x + pts[i].x * (hi.x - lo.x), Y(pts[i].y));
        const bool over = warnAbove > 0.0f && pts[i].y > warnAbove;
        dl->AddLine(a0, a1, over ? warn : line, 1.6f);
    }
    if (hovered && n > 0) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float tx = (mx - lo.x) / (hi.x - lo.x);
        int best = 0; float bd = 1e9f;
        for (int i = 0; i < n; ++i) { const float d = fabsf(pts[i].x - tx); if (d < bd) { bd = d; best = i; } }
        const float x = lo.x + pts[best].x * (hi.x - lo.x);
        dl->AddLine(ImVec2(x, lo.y), ImVec2(x, hi.y), theme::U(theme::kMuted, a * 0.6f), 1.0f);
        dl->AddCircleFilled(ImVec2(x, Y(pts[best].y)), 3.0f, theme::U(theme::kAccentHot, a));
        ImGui::SetTooltip("%.1f %s", pts[best].y, unit);
    }
    ImGui::PopID();
}

} // namespace eam::ui
