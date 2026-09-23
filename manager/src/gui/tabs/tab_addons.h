#pragma once
#include <string>

namespace eam {
class AddonManager;

void RenderTabAddons(AddonManager* manager);

// Queue an addon (folder, .zip or .dll) for installation: the confirmation opens on the Addons tab. Used by drag and drop.
void RequestInstallFromPath(const std::wstring& path);

} // namespace eam
