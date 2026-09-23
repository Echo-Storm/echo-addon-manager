#pragma once

namespace eam {
class AddonManager;

namespace window {

// One frame of the manager's content, inside an ImGui frame that is already begun: the full-window panel with the tab bar
// (Addons, Features, Performance, Settings, Logs, About) and the status bar along the bottom. `bringAddonsForward` is a request to
// select the Addons tab (a file was dropped); it is cleared once the tab is shown.
void RenderMainFrame(AddonManager* manager, bool& bringAddonsForward);

} // namespace window
} // namespace eam
