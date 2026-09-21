#pragma once
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace lsproxy {

using EventCallback = void(*)(uint32_t eventId, const void* data, uint32_t dataSize, void* userData);

class EventBus {
public:
    static EventBus& Instance();

    void Subscribe(uint32_t eventId, EventCallback callback, void* userData = nullptr);
    void Unsubscribe(uint32_t eventId, EventCallback callback);
    void Publish(uint32_t eventId, const void* data = nullptr, uint32_t dataSize = 0);

private:
    EventBus() = default;

    struct Subscriber {
        EventCallback callback;
        void* userData;
    };

    mutable std::shared_mutex m_mutex;
    std::unordered_map<uint32_t, std::vector<Subscriber>> m_subscribers;
};

} // namespace lsproxy
