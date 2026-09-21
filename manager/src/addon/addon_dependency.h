#pragma once
#include "addon_info.h"
#include <vector>

namespace lsproxy {

class AddonDependency {
public:
    // Sorts addons in dependency order (dependencies first).
    // Returns false if a cycle is detected.
    static bool Resolve(std::vector<AddonInfo>& addons);
};

} // namespace lsproxy
