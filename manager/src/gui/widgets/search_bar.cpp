#include "search_bar.h"

namespace lsproxy {
namespace widgets {

bool SearchBar(const char* id, char* buffer, size_t bufferSize, const char* hint) {
    ImGui::PushItemWidth(-1);
    bool changed = ImGui::InputTextWithHint(id, hint, buffer, bufferSize);
    ImGui::PopItemWidth();
    return changed;
}

} // namespace widgets
} // namespace lsproxy
