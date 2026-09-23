// Auto quality: keeps the model within a time budget by lowering its working scale (the "Model resolution") when it runs over, and raising it
// again toward the person's own setting when there is room. The person's setting is the ceiling, a floor they choose is the lowest it goes, and
// nothing that changes the look is touched.
//
// Every change of the working scale makes the model's feature again (a short hitch), so it changes slowly: down after 3 s over the budget (and
// at least 5 s after the last change), up after 10 s well under it (and 20 s after the last change), and only up when the model time it expects
// at the higher scale still fits. Frames slower than 100 ms (a loading screen, a pause) are not judged.
#pragma once
#include <cstdint>
#include <deque>

namespace nr {

class AutoQuality {
public:
    struct Settings { bool on = false; float budgetMs = 5.0f; float floor = 0.25f; };
    struct Step { uint64_t atMs; float from, to, modelMs; };

    // At every model run: the time now, the model's last time, the time between frames and the person's working scale. True when the scale
    // it wants changed (Scale() and the newest Step say to what, and why).
    bool Update(uint64_t nowMs, float modelMs, float frameIntervalMs, float ceiling, const Settings& s);
    // The working scale to run the model at: the person's own when auto is off.
    float Scale() const { return m_scale; }
    float AverageMs() const { return m_avgMs; }
    const std::deque<Step>& History() const { return m_history; }

private:
    bool Change(uint64_t nowMs, float to);
    float m_scale = 0;           // 0 until the first update
    float m_avgMs = 0;
    uint64_t m_overSince = 0, m_underSince = 0, m_lastChange = 0;
    std::deque<Step> m_history;  // the newest last, at most 8
};

} // namespace nr
