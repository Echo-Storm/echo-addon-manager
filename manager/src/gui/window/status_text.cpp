#include "status_text.h"
#include "../../../sdk/include/lsproxy/version.h"

namespace lsproxy {
namespace window {

std::string StatusCounts(int total, int on) {
    return std::string(LSPROXY_PRODUCT_NAME " " LSPROXY_VERSION_STRING "   |   ") + std::to_string(total) + (total == 1 ? " addon, " : " addons, ") +
           std::to_string(on) + " on";
}

std::string WithLiveStatus(std::string counts, const std::string& who, const std::string& text) {
    if (!text.empty()) counts += "   |   " + who + ": " + text;
    return counts;
}

} // namespace window
} // namespace lsproxy
