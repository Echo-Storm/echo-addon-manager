// Offline test of auto quality (auto_quality.cpp) with a simulated model: its time grows with the area it works on, about 2.7 ms plus 1.8 ms a
// megapixel (as measured on an RTX 4070 Ti SUPER), on 2560x1440 frames at 60 per second.
#include "addon/auto_quality.h"
#include <cmath>
#include <cstdio>
#include <string>

static int g_failed = 0;
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  (", detail.empty() ? "" : (detail + ")").c_str());
    if (!ok) ++g_failed;
}

static float ModelMs(float scale, float load) { return load * (2.7f + 1.8f * (2560.0f * scale) * (1440.0f * scale) / 1e6f); }

struct Run { int changes = 0; uint64_t shortestGap = ~0ull; float lowest = 9, highest = 0; };

// `seconds` of frames at 60 per second; `load` scales the model's time (a busier scene, a hotter card).
static Run Simulate(nr::AutoQuality& a, uint64_t& now, int seconds, float ceiling, const nr::AutoQuality::Settings& s, float load, float frameMs = 16.7f) {
    Run r; uint64_t last = 0;
    for (int i = 0; i < seconds * 60; ++i) {
        now += 17;
        const float scale = a.Scale() > 0 ? a.Scale() : ceiling;
        if (a.Update(now, ModelMs(scale, load), frameMs, ceiling, s)) {
            ++r.changes;
            if (last) r.shortestGap = std::min<uint64_t>(r.shortestGap, now - last);
            last = now;
        }
        r.lowest = std::min(r.lowest, a.Scale()); r.highest = std::max(r.highest, a.Scale());
    }
    return r;
}

int main() {
    using nr::AutoQuality;
    char text[160];
    printf("== a model over its budget\n");
    {
        AutoQuality a; uint64_t now = 0;
        const AutoQuality::Settings s{ true, 5.0f, 0.25f };
        const Run r = Simulate(a, now, 180, 1.0f, s, 1.0f);   // at 1.0 the model takes about 9.3 ms
        snprintf(text, sizeof text, "scale %.2f, model %.1f ms, %d changes", a.Scale(), ModelMs(a.Scale(), 1.0f), r.changes);
        Check("it lowers the model resolution until the model fits the budget", ModelMs(a.Scale(), 1.0f) <= 5.5f && a.Scale() >= 0.25f, text);
        Check("...without overshooting far below what fits", ModelMs(a.Scale() + 0.10f, 1.0f) > 5.0f * 0.9f, text);
        Check("...changing at most once every 5 seconds", r.shortestGap >= 5000, std::to_string(r.shortestGap) + " ms");
        const float settled = a.Scale();
        const Run later = Simulate(a, now, 120, 1.0f, s, 1.0f);
        Check("once it fits, it stays put (no swinging back and forth)", later.changes == 0 && a.Scale() == settled, std::to_string(later.changes) + " changes");

        const Run easier = Simulate(a, now, 300, 1.0f, s, 0.5f);   // the model gets twice as fast: room to go back up
        snprintf(text, sizeof text, "scale %.2f, model %.1f ms", a.Scale(), ModelMs(a.Scale(), 0.5f));
        Check("with room again it goes back up, still within the budget", a.Scale() > settled && ModelMs(a.Scale(), 0.5f) <= 5.0f, text);
        Check("...at most once every 20 seconds going up", easier.shortestGap >= 20000, std::to_string(easier.shortestGap) + " ms");
        Check("...and never above the person's own setting", easier.highest <= 1.0f);
    }

    printf("== limits\n");
    {
        AutoQuality a; uint64_t now = 0;
        Simulate(a, now, 300, 0.8f, { true, 1.0f, 0.4f }, 1.0f);   // a budget it can never meet
        Check("it never goes below the floor", std::fabs(a.Scale() - 0.4f) < 0.001f, std::to_string(a.Scale()));
        Simulate(a, now, 1, 0.3f, { true, 1.0f, 0.4f }, 1.0f);
        Check("a person's setting below the floor wins over the floor", a.Scale() <= 0.3f + 0.001f, std::to_string(a.Scale()));
    }
    {
        AutoQuality a; uint64_t now = 0;
        const Run r = Simulate(a, now, 60, 1.0f, { true, 5.0f, 0.25f }, 1.0f, 250.0f);   // a loading screen: 4 frames a second
        Check("frames slower than 100 ms (loading, a pause) are not judged", r.changes == 0 && a.Scale() == 1.0f);
    }
    {
        AutoQuality a; uint64_t now = 0;
        Simulate(a, now, 120, 1.0f, { true, 5.0f, 0.25f }, 1.0f);
        const Run off = Simulate(a, now, 1, 0.9f, { false, 5.0f, 0.25f }, 1.0f);
        Check("switched off, the person's own setting is used at once", a.Scale() == 0.9f && off.changes == 0);
    }
    {
        AutoQuality a; uint64_t now = 0;
        Simulate(a, now, 120, 1.0f, { true, 5.0f, 0.25f }, 1.0f);
        Check("each change is kept with its time, the scales and the model time", !a.History().empty() && a.History().back().to == a.Scale() &&
              a.History().back().from > a.History().back().to && a.History().back().modelMs > 5.0f);
    }

    printf("\n%s\n", g_failed ? "AUTO QUALITY TEST FAILED" : "AUTO QUALITY TEST PASSED");
    return g_failed ? 1 : 0;
}
