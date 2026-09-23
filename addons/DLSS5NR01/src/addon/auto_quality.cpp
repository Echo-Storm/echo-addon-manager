#include "addon/auto_quality.h"
#include <algorithm>

namespace nr {

namespace {
constexpr float kStep = 0.05f, kBigStep = 0.10f;
constexpr uint64_t kOverFor = 3000, kUnderFor = 10000, kDownPause = 5000, kUpPause = 20000;
constexpr float kPauseFrameMs = 100.0f;
}

bool AutoQuality::Change(uint64_t nowMs, float to) {
    m_history.push_back({ nowMs, m_scale, to, m_avgMs });
    if (m_history.size() > 8) m_history.pop_front();
    m_scale = to;
    m_lastChange = nowMs;
    m_overSince = m_underSince = 0;
    m_avgMs = 0;   // measured afresh at the new scale
    return true;
}

bool AutoQuality::Update(uint64_t nowMs, float modelMs, float frameIntervalMs, float ceiling, const Settings& s) {
    const float floor = std::min(s.floor, ceiling);
    if (!s.on || m_scale <= 0 || m_scale > ceiling) {   // off, the first time, or the person lowered their own setting below it
        const bool changed = m_scale != ceiling && m_scale > 0 && s.on;
        m_scale = ceiling;
        m_overSince = m_underSince = 0;
        if (!s.on) { m_avgMs = 0; return false; }
        return changed;
    }
    if (modelMs <= 0 || frameIntervalMs > kPauseFrameMs) { m_overSince = m_underSince = 0; return false; }
    m_avgMs = m_avgMs == 0 ? modelMs : m_avgMs * 0.9f + modelMs * 0.1f;

    if (m_avgMs > s.budgetMs * 1.1f && m_scale > floor + 0.001f) {
        m_underSince = 0;
        if (!m_overSince) m_overSince = nowMs;
        if (nowMs - m_overSince < kOverFor || (m_lastChange && nowMs - m_lastChange < kDownPause)) return false;
        const float step = m_avgMs > s.budgetMs * 1.5f ? kBigStep : kStep;
        return Change(nowMs, std::max(floor, m_scale - step));
    }
    if (m_avgMs < s.budgetMs * 0.7f && m_scale < ceiling - 0.001f) {
        m_overSince = 0;
        if (!m_underSince) m_underSince = nowMs;
        if (nowMs - m_underSince < kUnderFor || (m_lastChange && nowMs - m_lastChange < kUpPause)) return false;
        const float to = std::min(ceiling, m_scale + kStep);
        const float ratio = to / m_scale;
        if (m_avgMs * ratio * ratio > s.budgetMs * 0.9f) return false;   // the model's time grows with the area it works on
        return Change(nowMs, to);
    }
    m_overSince = m_underSince = 0;
    return false;
}

} // namespace nr
