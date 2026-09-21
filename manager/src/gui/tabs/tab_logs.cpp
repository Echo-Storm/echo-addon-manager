#include "tab_logs.h"
#include "../gui_scale.h"
#include "../gui_style.h"
#include "../../log/logger.h"
#include "../widgets/tooltip.h"
#include "imgui.h"
#include "lsproxy/lsp_widgets.h"
#include <chrono>

namespace lsproxy {

static bool s_autoScroll = true;
static int s_levelFilter = (int)LogLevel::Trace;

// The log can hold 10,000 entries; copying them every frame is wasteful. Keep a snapshot and
// rebuild it only when the log or the filter changed, and at most ten times a second.
static std::vector<LogEntry> s_entries;
static uint64_t s_snapshotRev = ~0ull;
static int s_snapshotFilter = -1;
static std::chrono::steady_clock::time_point s_snapshotAt;

static void RefreshSnapshot() {
    const auto now = std::chrono::steady_clock::now();
    const uint64_t rev = Logger::Instance().Revision();
    const bool filterChanged = (s_snapshotFilter != s_levelFilter);
    const bool logChanged = (s_snapshotRev != rev);
    if (!filterChanged && !(logChanged && now - s_snapshotAt > std::chrono::milliseconds(100))) return;

    s_entries = Logger::Instance().GetEntries((LogLevel)s_levelFilter);
    s_snapshotRev = rev;
    s_snapshotFilter = s_levelFilter;
    s_snapshotAt = now;
}

void RenderTabLogs() {
    // Filter controls
    ImGui::Checkbox("Auto-scroll", &s_autoScroll);
    widgets::Tip("Keep the newest line in view. Scroll up to stop following; scroll back to the bottom to resume.");
    ImGui::SameLine();

    const char* levels[] = { "Trace", "Debug", "Info", "Warn", "Error" };
    ImGui::SetNextItemWidth(S(110));
    ImGui::Combo("Min Level", &s_levelFilter, levels, 5);
    widgets::Tip("Show only messages at or above this level. This only filters the view; what gets recorded is set in Settings > Log Level.");
    ImGui::SameLine();

    if (lsp::Button("Clear", lsp::icons::kTrash)) {
        Logger::Instance().Clear();
    }
    widgets::Tip("Empty this list. EchoAddonManager.log on disk is not touched.");

    ImGui::Separator();

    RefreshSnapshot();
    const auto& entries = s_entries;

    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    ImFont* mono = MonoFont();
    if (mono) ImGui::PushFont(mono, 0.0f);

    ImGuiListClipper clipper;
    clipper.Begin((int)entries.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const auto& e = entries[i];

            // Color by level
            ImVec4 color;
            switch (e.level) {
                case LogLevel::Trace: color = ImVec4(0.4f, 0.4f, 0.4f, 1); break;
                case LogLevel::Debug: color = ImVec4(0.55f, 0.6f, 0.65f, 1); break;
                case LogLevel::Info:  color = ImVec4(0.75f, 0.75f, 0.75f, 1); break;
                case LogLevel::Warn:  color = ImVec4(0.941f, 0.741f, 0.259f, 1); break;
                case LogLevel::Error: color = ImVec4(0.816f, 0.502f, 0.502f, 1); break;
                default:              color = ImVec4(0.75f, 0.75f, 0.75f, 1); break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("[%s] [%s] %s",
                       Logger::LevelToString(e.level),
                       e.source.c_str(),
                       e.message.c_str());
            ImGui::PopStyleColor();
        }
    }

    if (s_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    if (mono) ImGui::PopFont();
    ImGui::EndChild();
}

} // namespace lsproxy
