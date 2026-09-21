#pragma once
#include "imgui.h"
#include <string>

namespace lsproxy {
namespace widgets {

// Renders a search input. Returns true if filter changed.
bool SearchBar(const char* id, char* buffer, size_t bufferSize, const char* hint = "Search...");

} // namespace widgets
} // namespace lsproxy
