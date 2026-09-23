#include "addon/hud_editor.h"
#include "addon/screenshot.h"
#include "addon/state.h"
#include "imgui.h"
#include <eam/widgets.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace nr {

namespace {

// The snapshot, as an image the manager drew for us (window thread only).
void* g_image = nullptr;
unsigned g_imageW = 0, g_imageH = 0;
uint64_t g_serial = 0;

// What the mouse is doing to which area. The area being changed lives in g_working until the mouse is let go.
enum class Grab { None, Move, Create, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
Grab g_grab = Grab::None;
int g_area = -1;
float g_working[4] = {};
float g_offsetU = 0, g_offsetV = 0;   // Move: where in the area it was taken hold of
float g_anchorU = 0, g_anchorV = 0;   // Create: where the drag began
constexpr float kMinSize = 0.01f;     // an area is at least 1 % of the frame each way

bool Inside(const float* r, float u, float v) { return u >= r[0] && u <= r[2] && v >= r[1] && v <= r[3]; }

// The edge or corner of an area under the pointer, within `du` / `dv` (the size of a handle, in fractions of the frame).
Grab EdgeAt(const float* r, float u, float v, float du, float dv) {
    const bool nearL = std::fabs(u - r[0]) <= du, nearR = std::fabs(u - r[2]) <= du, nearT = std::fabs(v - r[1]) <= dv, nearB = std::fabs(v - r[3]) <= dv;
    const bool withinU = u >= r[0] - du && u <= r[2] + du, withinV = v >= r[1] - dv && v <= r[3] + dv;
    if (nearL && nearT) return Grab::TopLeft;
    if (nearR && nearT) return Grab::TopRight;
    if (nearL && nearB) return Grab::BottomLeft;
    if (nearR && nearB) return Grab::BottomRight;
    if (nearL && withinV) return Grab::Left;
    if (nearR && withinV) return Grab::Right;
    if (nearT && withinU) return Grab::Top;
    if (nearB && withinU) return Grab::Bottom;
    return Grab::None;
}

ImGuiMouseCursor CursorFor(Grab g) {
    switch (g) {
    case Grab::Left: case Grab::Right: return ImGuiMouseCursor_ResizeEW;
    case Grab::Top: case Grab::Bottom: return ImGuiMouseCursor_ResizeNS;
    case Grab::TopLeft: case Grab::BottomRight: return ImGuiMouseCursor_ResizeNWSE;
    case Grab::TopRight: case Grab::BottomLeft: return ImGuiMouseCursor_ResizeNESW;
    case Grab::Move: return ImGuiMouseCursor_ResizeAll;
    default: return ImGuiMouseCursor_Arrow;
    }
}

void TakeNewSnapshot() {
    std::vector<unsigned char> rgba; unsigned w = 0, h = 0;
    if (!screenshot::NewSnapshot(g_serial, rgba, w, h) || !g_host || g_host->GetHostVersion() < 0x010200) return;
    DropHudSnapshot();
    g_image = g_host->CreateImage(rgba.data(), w, h, w * 4);
    g_imageW = w; g_imageH = h;
}

// The working area while it is dragged, from the pointer (u, v).
void Drag(float u, float v) {
    float* r = g_working;
    switch (g_grab) {
    case Grab::Move: {
        const float w = r[2] - r[0], h = r[3] - r[1];
        r[0] = std::clamp(u - g_offsetU, 0.0f, 1.0f - w); r[1] = std::clamp(v - g_offsetV, 0.0f, 1.0f - h);
        r[2] = r[0] + w; r[3] = r[1] + h;
        break;
    }
    case Grab::Create:
        r[0] = std::min(g_anchorU, u); r[1] = std::min(g_anchorV, v); r[2] = std::max(g_anchorU, u); r[3] = std::max(g_anchorV, v);
        break;
    default: {
        const bool left = g_grab == Grab::Left || g_grab == Grab::TopLeft || g_grab == Grab::BottomLeft;
        const bool right = g_grab == Grab::Right || g_grab == Grab::TopRight || g_grab == Grab::BottomRight;
        const bool top = g_grab == Grab::Top || g_grab == Grab::TopLeft || g_grab == Grab::TopRight;
        const bool bottom = g_grab == Grab::Bottom || g_grab == Grab::BottomLeft || g_grab == Grab::BottomRight;
        if (left) r[0] = std::min(u, r[2] - kMinSize);
        if (right) r[2] = std::max(u, r[0] + kMinSize);
        if (top) r[1] = std::min(v, r[3] - kMinSize);
        if (bottom) r[3] = std::max(v, r[1] + kMinSize);
        break;
    }
    }
}

void DrawArea(ImDrawList* dl, const ImVec2& p0, const ImVec2& size, const float* r, int number, bool active) {
    const ImVec2 a(p0.x + r[0] * size.x, p0.y + r[1] * size.y), b(p0.x + r[2] * size.x, p0.y + r[3] * size.y);
    const ImU32 green = IM_COL32(125, 179, 66, 255), fill = IM_COL32(125, 179, 66, active ? 90 : 55);
    dl->AddRectFilled(a, b, fill);
    dl->AddRect(a, b, green, 0.0f, 0, active ? 2.5f : 1.5f);
    const float handle = ImGui::GetFontSize() * 0.3f;
    for (const ImVec2& c : { a, ImVec2(b.x, a.y), ImVec2(a.x, b.y), b }) dl->AddRectFilled(ImVec2(c.x - handle, c.y - handle), ImVec2(c.x + handle, c.y + handle), green);
    char label[8]; snprintf(label, sizeof label, "%d", number);
    dl->AddText(ImVec2(a.x + handle * 1.5f, a.y + handle * 1.2f), IM_COL32(255, 255, 255, 230), label);
}

} // namespace

void DropHudSnapshot() {
    if (g_image && g_host && g_host->GetHostVersion() >= 0x010200) g_host->ReleaseImage(g_image);
    g_image = nullptr;
}

bool DrawHudEditor(NrParams& p) {
    TakeNewSnapshot();
    const float width = ImGui::GetContentRegionAvail().x;
    float aspect = 9.0f / 16.0f;
    if (g_image && g_imageW) aspect = static_cast<float>(g_imageH) / g_imageW;
    else if (g_bridge.Width()) aspect = static_cast<float>(g_bridge.Height()) / g_bridge.Width();
    const ImVec2 size(width, width * aspect), p0 = ImGui::GetCursorScreenPos(), p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_image) dl->AddImage(reinterpret_cast<ImTextureID>(g_image), p0, p1);
    else {
        dl->AddRectFilled(p0, p1, IM_COL32(28, 28, 28, 255));
        const char* hint = "Take a snapshot to see the game here. You can draw areas meanwhile.";
        const ImVec2 t = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(p0.x + (size.x - t.x) * 0.5f, p0.y + (size.y - t.y) * 0.5f), IM_COL32(150, 150, 150, 255), hint);
    }
    dl->AddRect(p0, p1, IM_COL32(90, 90, 90, 255));

    ImGui::InvisibleButton("##hudcanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float u = std::clamp((m.x - p0.x) / size.x, 0.0f, 1.0f), v = std::clamp((m.y - p0.y) / size.y, 0.0f, 1.0f);
    const float handle = ImGui::GetFontSize() * 0.45f, du = handle / size.x, dv = handle / size.y;
    const int count = static_cast<int>(std::min<uint32_t>(p.hudCount, NrParams::kMaxHud));
    bool changed = false;

    // what is under the pointer: an edge or corner first (the topmost area wins), then an area's inside
    Grab under = Grab::None; int underArea = -1;
    for (int i = count - 1; i >= 0 && under == Grab::None; --i) if ((under = EdgeAt(p.hud[i], u, v, du, dv)) != Grab::None) underArea = i;
    for (int i = count - 1; i >= 0 && under == Grab::None; --i) if (Inside(p.hud[i], u, v)) { under = Grab::Move; underArea = i; }

    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (underArea >= 0) {
            g_grab = under; g_area = underArea;
            memcpy(g_working, p.hud[g_area], sizeof g_working);
            g_offsetU = u - g_working[0]; g_offsetV = v - g_working[1];
        } else if (count < NrParams::kMaxHud) {
            g_grab = Grab::Create; g_area = count;
            g_anchorU = u; g_anchorV = v;
            g_working[0] = g_working[2] = u; g_working[1] = g_working[3] = v;
        }
    }
    if (g_grab != Grab::None && ImGui::IsItemActive()) Drag(u, v);
    if (g_grab != Grab::None && ImGui::IsItemDeactivated()) {
        const bool bigEnough = g_working[2] - g_working[0] >= kMinSize && g_working[3] - g_working[1] >= kMinSize;
        if (g_grab == Grab::Create) { if (bigEnough) { memcpy(p.hud[count], g_working, sizeof g_working); p.hudCount = count + 1; changed = true; } }
        else { memcpy(p.hud[g_area], g_working, sizeof g_working); changed = true; }
        g_grab = Grab::None; g_area = -1;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && under == Grab::Move) {
        for (int k = underArea; k + 1 < count; ++k) memcpy(p.hud[k], p.hud[k + 1], sizeof p.hud[k]);
        p.hudCount = count - 1;
        changed = true;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        const Grab shown = g_grab != Grab::None ? (g_grab == Grab::Create ? Grab::None : g_grab) : under;
        ImGui::SetMouseCursor(g_grab == Grab::Create ? ImGuiMouseCursor_Arrow : CursorFor(shown));
    }

    dl->PushClipRect(p0, p1, true);
    for (int i = 0; i < static_cast<int>(std::min<uint32_t>(p.hudCount, NrParams::kMaxHud)); ++i)
        DrawArea(dl, p0, size, g_grab != Grab::None && g_area == i ? g_working : p.hud[i], i + 1, g_area == i || underArea == i);
    if (g_grab == Grab::Create) DrawArea(dl, p0, size, g_working, count + 1, true);
    dl->PopClipRect();
    return changed;
}

} // namespace nr
