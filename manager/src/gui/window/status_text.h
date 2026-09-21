#pragma once
#include <string>

namespace lsproxy {
namespace window {

// The left side of the status bar: "<product> <version>   |   N addon(s), M on".
std::string StatusCounts(int total, int on);

// Adds the most relevant live status of any addon, as "   |   <who>: <text>"; nothing when there is none.
std::string WithLiveStatus(std::string counts, const std::string& who, const std::string& text);

} // namespace window
} // namespace lsproxy
