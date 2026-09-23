#include "addon/settings.h"
#include <eam/addon_sdk.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nr {

namespace {

// The settings of NrParams, each once: its key, its range (FLT_MAX: the model takes any value), and whether it is part of a look.
struct FloatSetting { const char* key; float NrParams::* field; float lo, hi; bool inLook; };
struct UIntSetting { const char* key; uint32_t NrParams::* field; uint32_t lo, hi; bool inLook; };

const FloatSetting kFloats[] = {
    { "intensity",        &NrParams::intensity,        -FLT_MAX, FLT_MAX, true },   // the model keeps it to 0..1 itself
    { "localStructure",   &NrParams::localStructure,   -FLT_MAX, FLT_MAX, true },
    { "localTone",        &NrParams::localTone,        -FLT_MAX, FLT_MAX, true },
    { "skinStructure",    &NrParams::skinStructure,    -FLT_MAX, FLT_MAX, true },
    { "workingScale",     &NrParams::workingScale,     0.25f, 1.0f,  true },
    { "composeIntensity", &NrParams::composeIntensity, -FLT_MAX, FLT_MAX, true },
    { "maxDelta",         &NrParams::maxDelta,         -FLT_MAX, FLT_MAX, true },
    { "hiProtect",        &NrParams::hiProtect,        -FLT_MAX, FLT_MAX, true },
    { "sharpen",          &NrParams::sharpen,          0.0f, 1.0f,   true },
    { "saturation",       &NrParams::saturation,       0.0f, 2.0f,   true },
    { "vibrance",         &NrParams::vibrance,         0.0f, 1.0f,   true },
    { "brightness",       &NrParams::brightness,       -0.3f, 0.3f,  true },
    { "contrast",         &NrParams::contrast,         0.5f, 1.5f,   true },
    { "gamma",            &NrParams::gamma,            0.5f, 2.0f,   true },
    { "shadows",          &NrParams::shadows,          -1.0f, 1.0f,  true },
    { "highlights",       &NrParams::highlights,       -1.0f, 1.0f,  true },
    { "grain",            &NrParams::grain,            0.0f, 1.0f,   true },
    { "grainSize",        &NrParams::grainSize,        1.0f, 4.0f,   true },
    { "deltaSmooth",      &NrParams::deltaSmooth,      0.0f, 0.95f,  true },
    { "ghostGuard",       &NrParams::ghostGuard,       0.0f, 1.0f,   true },
    { "hudFeather",       &NrParams::hudFeather,       0.0f, 0.05f,  true },
    { "flowUnit",         &NrParams::flowUnit,         -FLT_MAX, FLT_MAX, false },
};
const UIntSetting kUInts[] = {
    { "passes",    &NrParams::passes,      1, 4, true },
    { "style",     &NrParams::style,       0, 2, true },
    { "autoMask",  &NrParams::useAutoMask, 0, 1, true },
    { "debugView", &NrParams::debugView,   0, 5, false },
};

float Limit(float v, const FloatSetting& s) { return std::clamp(v, s.lo, s.hi); }
uint32_t Limit(long long v, const UIntSetting& s) { return static_cast<uint32_t>(std::clamp<long long>(v, s.lo, s.hi)); }

std::string Number(float v) { char text[32]; snprintf(text, sizeof text, "%g", v); return text; }

// "a|b|c" and back.
std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> out;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('|', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}
std::string JoinList(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& s : items) { if (!out.empty()) out += '|'; out += s; }
    return out;
}

} // namespace

std::string HudToText(const NrParams& p) {
    std::string text;
    for (uint32_t i = 0; i < p.hudCount && i < static_cast<uint32_t>(NrParams::kMaxHud); ++i) {
        char area[96];
        snprintf(area, sizeof area, "%s%g,%g,%g,%g", i ? "/" : "", p.hud[i][0], p.hud[i][1], p.hud[i][2], p.hud[i][3]);
        text += area;
    }
    return text;
}

// Areas outside 0..1 are pulled in; an area with no width or height is left out.
void HudFromText(const std::string& text, NrParams& p) {
    p.hudCount = 0;
    for (size_t start = 0; start < text.size() && p.hudCount < static_cast<uint32_t>(NrParams::kMaxHud);) {
        size_t end = text.find('/', start);
        if (end == std::string::npos) end = text.size();
        float r[4] = {};
        if (sscanf(text.substr(start, end - start).c_str(), "%f,%f,%f,%f", &r[0], &r[1], &r[2], &r[3]) == 4) {
            for (float& v : r) v = std::clamp(v, 0.0f, 1.0f);
            if (r[2] > r[0] + 0.001f && r[3] > r[1] + 0.001f) memcpy(p.hud[p.hudCount++], r, sizeof r);
        }
        start = end + 1;
    }
}

std::string LookToText(const NrParams& p) {
    std::string text;
    for (const UIntSetting& s : kUInts) if (s.inLook) text += std::string(s.key) + "=" + std::to_string(p.*s.field) + ";";
    text += std::string("useFlow=") + (p.useFlow ? "1" : "0") + ";";
    for (const FloatSetting& s : kFloats) if (s.inLook) text += std::string(s.key) + "=" + Number(p.*s.field) + ";";
    return text + "hud=" + HudToText(p);
}

bool ApplyLook(const std::string& text, NrParams& p) {
    bool any = false;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(start, end - start);
        start = end + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
        bool known = false;
        if (key == "hud") { HudFromText(value, p); known = true; }
        else if (key == "useFlow") { p.useFlow = atof(value.c_str()) != 0.0; known = true; }
        for (const FloatSetting& s : kFloats) if (s.inLook && key == s.key) { p.*s.field = Limit(static_cast<float>(atof(value.c_str())), s); known = true; }
        for (const UIntSetting& s : kUInts) if (s.inLook && key == s.key) { p.*s.field = Limit(static_cast<long long>(atof(value.c_str())), s); known = true; }
        any |= known;
    }
    return any;
}

std::string CleanName(std::string name) {
    for (char& c : name) if (c == '|' || c == ';' || c == '=') c = '-';
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.size() > 40) name.resize(40);
    return name;
}

Loaded LoadSettings(IHost* host, const char* id) {
    auto text = [&](const std::string& key, const char* dflt = "") { return std::string(host ? host->GetConfig(id, key.c_str(), dflt) : dflt); };
    auto number = [&](const char* key, double dflt) { const std::string s = text(key); return s.empty() ? dflt : atof(s.c_str()); };
    auto flag = [&](const char* key, bool dflt) { return number(key, dflt ? 1 : 0) != 0.0; };
    auto integer = [&](const char* key, int dflt) { return static_cast<int>(number(key, dflt)); };

    Loaded out;
    Config& c = out.config;
    const NrParams defaults;
    for (const FloatSetting& s : kFloats) c.p.*s.field = Limit(static_cast<float>(number(s.key, defaults.*s.field)), s);
    for (const UIntSetting& s : kUInts) c.p.*s.field = Limit(static_cast<long long>(number(s.key, defaults.*s.field)), s);
    c.p.useFlow = flag("useFlow", true);
    HudFromText(text("hud"), c.p);

    c.enabled = flag("enabled", true);
    c.lsFirst = flag("lsFirst", true);
    c.freshFlow = flag("freshFlow", true);
    c.hotkeys = flag("hotkeys", true);
    c.keyAB = integer("keyAB", VK_F6); c.keySplit = integer("keySplit", VK_F7); c.keySharpDn = integer("keySharpDn", VK_F8);
    c.keySharpUp = integer("keySharpUp", VK_F9); c.keyPreset = integer("keyPreset", VK_F10);
    c.gameAuto = flag("gameAuto", true);
    for (const std::string& exe : SplitList(text("gameList"))) {
        const std::string look = text("game." + exe);
        if (!look.empty()) c.games.push_back({ exe, look });
    }
    c.tapMode = integer("tapMode", 0); c.frameSlot = integer("frameSlot", -1);
    c.tickSig = text("tickSig"); c.tapSig = text("tapSig");
    c.watchdogMs = static_cast<float>(number("watchdogMs", 80.0));
    c.snippetPath = text("snippetPath");
    for (const std::string& name : SplitList(text("presetNames"))) {
        const std::string data = text("preset." + name);
        if (!data.empty()) out.looks.push_back({ name, data });
    }
    // the view to start in: for the offline test host only (the panel and the hotkeys change it, nothing saves it)
    out.compareStart = std::clamp(integer("compareStart", 0), 0, 2);
    out.splitStart = std::clamp(static_cast<float>(number("splitStart", 0.5)), 0.05f, 0.95f);
    return out;
}

void SaveSettings(IHost* host, const char* id, const Config& c, const std::vector<Look>& looks) {
    if (!host) return;
    auto put = [&](const std::string& key, const std::string& value) { host->SetConfig(id, key.c_str(), value.c_str()); };
    auto putFlag = [&](const char* key, bool v) { put(key, v ? "1" : "0"); };
    for (const FloatSetting& s : kFloats) put(s.key, Number(c.p.*s.field));
    for (const UIntSetting& s : kUInts) put(s.key, std::to_string(c.p.*s.field));
    putFlag("useFlow", c.p.useFlow);
    put("hud", HudToText(c.p));

    putFlag("enabled", c.enabled); putFlag("lsFirst", c.lsFirst); putFlag("freshFlow", c.freshFlow); putFlag("hotkeys", c.hotkeys);
    put("keyAB", std::to_string(c.keyAB)); put("keySplit", std::to_string(c.keySplit)); put("keySharpDn", std::to_string(c.keySharpDn));
    put("keySharpUp", std::to_string(c.keySharpUp)); put("keyPreset", std::to_string(c.keyPreset));
    putFlag("gameAuto", c.gameAuto);
    std::vector<std::string> exes;
    for (const auto& [exe, look] : c.games) { exes.push_back(exe); put("game." + exe, look); }
    put("gameList", JoinList(exes));
    put("tapMode", std::to_string(c.tapMode)); put("frameSlot", std::to_string(c.frameSlot));
    put("tickSig", c.tickSig); put("tapSig", c.tapSig);
    put("watchdogMs", Number(c.watchdogMs)); put("snippetPath", c.snippetPath);
    std::vector<std::string> names;
    for (const Look& look : looks) { names.push_back(look.name); put("preset." + look.name, look.data); }
    put("presetNames", JoinList(names));
    host->SaveConfig();
}

void ForgetLook(IHost* host, const char* id, const std::string& name) { if (host) host->SetConfig(id, ("preset." + name).c_str(), ""); }
void ForgetGame(IHost* host, const char* id, const std::string& exe) { if (host) host->SetConfig(id, ("game." + exe).c_str(), ""); }

} // namespace nr
