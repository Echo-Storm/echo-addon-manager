#include "addon_manifest.h"
#include "../../third_party/nlohmann/json.hpp"
#include <cstdlib>
#include <fstream>

namespace lsproxy {

namespace {

using Json = nlohmann::json;

void TakeText(const Json& obj, const char* key, std::string& into) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_string()) into = it->get<std::string>();
}

void TakeList(const Json& obj, const char* key, std::vector<std::string>& into) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) return;
    for (const Json& item : *it)
        if (item.is_string()) into.push_back(item.get<std::string>());
}

} // namespace

bool ReadManifest(const std::filesystem::path& file, AddonManifest& out, std::string* problem) {
    auto fail = [&](const char* why) { if (problem) *problem = why; return false; };

    std::ifstream in(file);
    if (!in) return fail("the file could not be opened");
    const Json doc = Json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) return fail("it is not valid JSON");
    if (!doc.is_object()) return fail("it is not a JSON object");

    TakeText(doc, "name", out.name);
    TakeText(doc, "version", out.version);
    TakeText(doc, "author", out.author);
    TakeText(doc, "description", out.description);
    TakeText(doc, "min_host_version", out.minHostVersion);
    TakeText(doc, "dll", out.dll);
    TakeText(doc, "icon", out.icon);
    TakeList(doc, "dependencies", out.dependencies);
    TakeList(doc, "tags", out.tags);
    out.parsed = true;
    return true;
}

uint32_t ParseApiVersion(const std::string& text) {
    uint32_t part[3] = { 0, 0, 0 };
    int have = 0;
    const char* at = text.c_str();
    while (have < 3) {
        char* after = nullptr;
        const unsigned long v = std::strtoul(at, &after, 10);
        if (after == at) break;
        part[have++] = (uint32_t)v;
        at = after;
        if (*at != '.') break;
        ++at;
    }
    if (have < 2) return 0;
    return (part[0] << 16) | ((part[1] & 0xFF) << 8) | (part[2] & 0xFF);
}

} // namespace lsproxy
