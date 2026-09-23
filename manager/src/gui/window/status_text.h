#pragma once
#include <string>

namespace eam {
namespace window {

// The left side of the status bar: "<product> <version>   |   N addon(s), M on".
std::string StatusCounts(int total, int on);

// Adds "   |   Update available: <version>" when a newer version has been found; nothing when `latest` is empty.
std::string WithUpdate(std::string line, const std::string& latest);

// Adds the most relevant live status of any addon, as "   |   <who>: <text>"; nothing when there is none.
std::string WithLiveStatus(std::string counts, const std::string& who, const std::string& text);

} // namespace window
} // namespace eam
