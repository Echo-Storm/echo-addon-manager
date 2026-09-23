#include "metrics.h"
#include <algorithm>

namespace eam {

Metrics& Metrics::Instance() { static Metrics m; return m; }
Metrics::Metrics() : m_t0(std::chrono::steady_clock::now()) {}

double Metrics::Now() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_t0).count(); }

void Metrics::SetStatus(const std::string& addon, const std::string& text, int level) {
    if (addon.empty()) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& s : m_status) if (s.addon == addon) { s.text = text; s.level = std::clamp(level, 0, 3); s.at = Now(); return; }
    m_status.push_back({ addon, text, std::clamp(level, 0, 3), Now() });
}

void Metrics::Publish(const std::string& addon, const std::string& key, double value, const std::string& unit) { PublishAt(addon, key, value, unit, Now()); }

void Metrics::PublishAt(const std::string& addon, const std::string& key, double value, const std::string& unit, double t) {
    if (addon.empty() || key.empty()) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    Store* st = nullptr;
    for (auto& s : m_series) if (s.key == key && s.addon == addon) { st = &s; break; }
    if (!st) {
        if (m_series.size() >= 256) return;   // a misbehaving addon cannot grow this without bound
        m_series.push_back({ addon, key, unit, std::vector<Sample>(kMaxSamples), 0, 0 });
        st = &m_series.back();
    }
    if (!unit.empty()) st->unit = unit;
    st->ring[st->head] = { t, (float)value };
    st->head = (st->head + 1) % kMaxSamples;
    if (st->count < kMaxSamples) ++st->count;
}

static Metrics::Series Copy(const std::string& addon, const std::string& key, const std::string& unit, const std::vector<Metrics::Sample>& ring,
                            size_t head, size_t count, size_t cap, double minT) {
    Metrics::Series out; out.addon = addon; out.key = key; out.unit = unit;
    out.samples.reserve(count);
    const size_t start = (head + cap - count) % cap;
    for (size_t i = 0; i < count; ++i) {
        const Metrics::Sample& s = ring[(start + i) % cap];
        if (s.t >= minT) out.samples.push_back(s);
    }
    return out;
}

std::vector<Metrics::Series> Metrics::Snapshot(double windowSeconds) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    const double minT = Now() - windowSeconds;
    std::vector<Series> out;
    for (const auto& s : m_series) out.push_back(Copy(s.addon, s.key, s.unit, s.ring, s.head, s.count, kMaxSamples, minT));
    return out;
}

Metrics::Series Metrics::Get(const std::string& addon, const std::string& key, double windowSeconds) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    const double minT = Now() - windowSeconds;
    for (const auto& s : m_series) if (s.key == key && s.addon == addon) return Copy(s.addon, s.key, s.unit, s.ring, s.head, s.count, kMaxSamples, minT);
    Series none; none.addon = addon; none.key = key; return none;
}

Metrics::Status Metrics::GetStatus(const std::string& addon, double staleSeconds) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& s : m_status) if (s.addon == addon) {
        const double age = Now() - s.at;
        if (age > staleSeconds || s.text.empty()) return {};
        return { s.addon, s.text, s.level, age };
    }
    return {};
}

Metrics::Status Metrics::BestStatus(double staleSeconds) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    Status best; bool have = false;
    for (const auto& s : m_status) {
        const double age = Now() - s.at;
        if (age > staleSeconds || s.text.empty()) continue;
        if (!have || s.level > best.level || (s.level == best.level && age < best.ageSeconds)) { best = { s.addon, s.text, s.level, age }; have = true; }
    }
    return best;
}

void Metrics::Clear() { std::lock_guard<std::mutex> lk(m_mutex); m_series.clear(); m_status.clear(); }

} // namespace eam
