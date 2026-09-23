#include "tab_logs.h"
#include "../gui_scale.h"
#include "../gui_style.h"
#include "../../log/logger.h"
#include "../../../sdk/include/eam/version.h"
#include "../widgets/tooltip.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <chrono>
#include <vector>

namespace eam {

namespace {

bool g_followNewest = true;
int g_minLevel = (int)LogLevel::Trace;

// The log holds up to 10,000 entries and copying them every frame would be wasteful, so the tab keeps a snapshot and takes a new one only
// when the filter changed, or the log did and the last snapshot is more than a tenth of a second old.
struct Snapshot {
    std::vector<LogEntry> entries;
    uint64_t revision = ~0ull;
    int level = -1;
    std::chrono::steady_clock::time_point takenAt;
} g_snap;

const std::vector<LogEntry>& CurrentEntries() {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    const uint64_t revision = Logger::Instance().Revision();
    const bool filterChanged = g_snap.level != g_minLevel;
    const bool logMoved = g_snap.revision != revision && now - g_snap.takenAt > milliseconds(100);
    if (filterChanged || logMoved) {
        g_snap.entries = Logger::Instance().GetEntries((LogLevel)g_minLevel);
        g_snap.revision = revision;
        g_snap.level = g_minLevel;
        g_snap.takenAt = now;
    }
    return g_snap.entries;
}

ImVec4 ColourFor(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return ImVec4(0.4f, 0.4f, 0.4f, 1);
        case LogLevel::Debug: return ImVec4(0.55f, 0.6f, 0.65f, 1);
        case LogLevel::Warn:  return ImVec4(0.941f, 0.741f, 0.259f, 1);
        case LogLevel::Error: return ImVec4(0.816f, 0.502f, 0.502f, 1);
        default:              return ImVec4(0.75f, 0.75f, 0.75f, 1);   // Info
    }
}

void DrawControls() {
    ImGui::Checkbox("Auto-scroll", &g_followNewest);
    widgets::Tip("Keep the newest line in view. Scroll up to stop following; scroll back to the bottom to resume.");
    ImGui::SameLine();

    static const char* const kLevels[] = { "Trace", "Debug", "Info", "Warn", "Error" };
    ImGui::SetNextItemWidth(S(110));
    ImGui::Combo("Min Level", &g_minLevel, kLevels, 5);
    widgets::Tip("Show only messages at or above this level. This only filters the view; what gets recorded is set in Settings > Log Level.");
    ImGui::SameLine();

    if (eam::ui::Button("Clear", eam::ui::icons::kTrash)) Logger::Instance().Clear();
    widgets::Tip("Empty this list. " EAM_PRODUCT_FILE ".log on disk is not touched.");
    ImGui::Separator();
}

} // namespace

void RenderTabLogs() {
    DrawControls();
    const std::vector<LogEntry>& entries = CurrentEntries();

    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImFont* mono = MonoFont();
    if (mono) ImGui::PushFont(mono, 0.0f);

    ImGuiListClipper clipper;   // only the lines in view are drawn
    clipper.Begin((int)entries.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const LogEntry& e = entries[i];
            ImGui::PushStyleColor(ImGuiCol_Text, ColourFor(e.level));
            ImGui::Text("[%s] [%s] %s", Logger::LevelToString(e.level), e.source.c_str(), e.message.c_str());
            ImGui::PopStyleColor();
        }
    }

    if (g_followNewest && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);

    if (mono) ImGui::PopFont();
    ImGui::EndChild();
}

} // namespace eam
