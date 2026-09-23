#pragma once
#include "addon_info.h"
#include <vector>

namespace eam {

class AddonDependency {
public:
    // Reorders `addons` so that each one comes after the addons it lists as dependencies; addons that do not depend on each other keep
    // the order they had. A dependency that is not installed is noted in the log and otherwise ignored. If the addons depend on each
    // other in a circle there is no valid order: this logs it, returns false, and leaves the list as it was.
    static bool Resolve(std::vector<AddonInfo>& addons);
};

} // namespace eam
