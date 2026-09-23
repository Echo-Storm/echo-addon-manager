#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace eam {

// What addons publish through IHost::SetStatus / PublishMetric, and what the GUI reads. All calls are thread-safe.
class Metrics {
public:
    static Metrics& Instance();

    struct Sample { double t; float v; };   // t = seconds on the steady clock since the registry was created
    struct Series {
        std::string addon, key, unit;
        std::vector<Sample> samples;        // oldest first (a copy, safe to keep)
        float last() const { return samples.empty() ? 0.0f : samples.back().v; }
    };
    struct Status { std::string addon, text; int level = 0; double ageSeconds = 0; };

    void SetStatus(const std::string& addon, const std::string& text, int level);
    void Publish(const std::string& addon, const std::string& key, double value, const std::string& unit);
    // Same, with an explicit timestamp (seconds on this registry's clock, see Now()): for tests and offline previews.
    void PublishAt(const std::string& addon, const std::string& key, double value, const std::string& unit, double t);

    // A copy of every series, keeping only samples newer than `windowSeconds`.
    std::vector<Series> Snapshot(double windowSeconds) const;
    // One series by name, same window; an empty series (no samples) when it does not exist.
    Series Get(const std::string& addon, const std::string& key, double windowSeconds) const;
    // The current status of an addon (empty text when none or stale: not refreshed for `staleSeconds`).
    Status GetStatus(const std::string& addon, double staleSeconds = 6.0) const;
    // The most relevant live status of any addon, for the status bar (highest level first, then the newest); empty text when none.
    Status BestStatus(double staleSeconds = 6.0) const;
    double Now() const;
    void Clear();   // for tests

    static constexpr size_t kMaxSamples = 4000;   // per series (about a minute of a 60 Hz metric)

private:
    Metrics();
    struct Store {
        std::string addon, key, unit;
        std::vector<Sample> ring; size_t head = 0; size_t count = 0;   // ring buffer
    };
    struct StatusStore { std::string addon, text; int level = 0; double at = 0; };
    mutable std::mutex m_mutex;
    std::vector<Store> m_series;
    std::vector<StatusStore> m_status;
    std::chrono::steady_clock::time_point m_t0;
};

} // namespace eam
