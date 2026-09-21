#include "addon_dependency.h"
#include "../log/logger.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace lsproxy {

bool AddonDependency::Resolve(std::vector<AddonInfo>& addons) {
    // Build index: addon id -> position
    std::unordered_map<std::string, size_t> indexMap;
    for (size_t i = 0; i < addons.size(); i++) {
        indexMap[addons[i].id] = i;
    }

    // Topological sort (Kahn's algorithm)
    size_t n = addons.size();
    std::vector<std::vector<size_t>> adj(n);
    std::vector<int> inDegree(n, 0);

    for (size_t i = 0; i < n; i++) {
        for (const auto& dep : addons[i].manifest.dependencies) {
            auto it = indexMap.find(dep);
            if (it == indexMap.end()) {
                LOG_WARN("Dependency", "Addon '%s' depends on '%s' which is not installed",
                         addons[i].id.c_str(), dep.c_str());
                continue;
            }
            adj[it->second].push_back(i); // dep -> dependent
            inDegree[i]++;
        }
    }

    std::vector<size_t> order;
    std::vector<size_t> queue;
    for (size_t i = 0; i < n; i++) {
        if (inDegree[i] == 0) queue.push_back(i);
    }

    while (!queue.empty()) {
        size_t cur = queue.back();
        queue.pop_back();
        order.push_back(cur);
        for (size_t next : adj[cur]) {
            if (--inDegree[next] == 0) {
                queue.push_back(next);
            }
        }
    }

    if (order.size() != n) {
        LOG_ERROR("Dependency", "Circular dependency detected among addons!");
        return false;
    }

    // Reorder addons vector
    std::vector<AddonInfo> sorted;
    sorted.reserve(n);
    for (size_t idx : order) {
        sorted.push_back(std::move(addons[idx]));
    }
    addons = std::move(sorted);
    return true;
}

} // namespace lsproxy
