#include "addon_dependency.h"
#include "../log/logger.h"
#include <unordered_set>

namespace lsproxy {

bool AddonDependency::Resolve(std::vector<AddonInfo>& addons) {
    std::unordered_set<std::string> installed;
    for (const AddonInfo& a : addons) installed.insert(a.id);

    for (const AddonInfo& a : addons)
        for (const std::string& dep : a.manifest.dependencies)
            if (!installed.count(dep))
                LOG_WARN("Dependency", "Addon '%s' depends on '%s' which is not installed", a.id.c_str(), dep.c_str());

    // Sweep the list in its current order, taking every addon whose installed dependencies have all been taken. Repeat until none is
    // left; a sweep that takes nothing means the rest wait on each other.
    std::vector<size_t> order;
    std::vector<bool> taken(addons.size(), false);
    std::unordered_set<std::string> takenIds;
    while (order.size() < addons.size()) {
        const size_t before = order.size();
        for (size_t i = 0; i < addons.size(); ++i) {
            if (taken[i]) continue;
            bool ready = true;
            for (const std::string& dep : addons[i].manifest.dependencies)
                if (installed.count(dep) && !takenIds.count(dep)) { ready = false; break; }
            if (!ready) continue;
            taken[i] = true;
            takenIds.insert(addons[i].id);
            order.push_back(i);
        }
        if (order.size() == before) {
            LOG_ERROR("Dependency", "Circular dependency detected among addons!");
            return false;
        }
    }

    std::vector<AddonInfo> sorted;
    sorted.reserve(addons.size());
    for (size_t i : order) sorted.push_back(std::move(addons[i]));
    addons = std::move(sorted);
    return true;
}

} // namespace lsproxy
