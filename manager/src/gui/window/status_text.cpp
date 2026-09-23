#include "status_text.h"
#include "../../../sdk/include/eam/version.h"

namespace eam {
namespace window {

std::string StatusCounts(int total, int on) {
    return std::string(EAM_PRODUCT_NAME " " EAM_VERSION_STRING "   |   ") + std::to_string(total) + (total == 1 ? " addon, " : " addons, ") +
           std::to_string(on) + " on";
}

std::string WithUpdate(std::string line, const std::string& latest) {
    if (!latest.empty()) line += "   |   Update available: " + latest;
    return line;
}

std::string WithLiveStatus(std::string counts, const std::string& who, const std::string& text) {
    if (!text.empty()) counts += "   |   " + who + ": " + text;
    return counts;
}

} // namespace window
} // namespace eam
