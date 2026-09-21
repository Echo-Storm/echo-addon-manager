#include "event_system.h"
#include "../log/logger.h"
#include <algorithm>
#include <mutex>
#include <windows.h>

namespace lsproxy {

namespace {

// Kept apart from anything that owns C++ objects: a function with a __try block cannot also have destructors to run.
bool CallGuarded(EventCallback callback, uint32_t eventId, const void* data, uint32_t size, void* userData) {
    __try {
        callback(eventId, data, size, userData);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

EventBus& EventBus::Instance() {
    static EventBus bus;
    return bus;
}

void EventBus::Subscribe(uint32_t eventId, EventCallback callback, void* userData) {
    if (!callback) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_subscribers[eventId].push_back({ callback, userData });
}

void EventBus::Unsubscribe(uint32_t eventId, EventCallback callback) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    const auto list = m_subscribers.find(eventId);
    if (list == m_subscribers.end()) return;
    auto& subs = list->second;
    subs.erase(std::remove_if(subs.begin(), subs.end(), [callback](const Subscriber& s) { return s.callback == callback; }), subs.end());
}

void EventBus::Publish(uint32_t eventId, const void* data, uint32_t dataSize) {
    std::vector<Subscriber> subscribers;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto list = m_subscribers.find(eventId);
        if (list == m_subscribers.end()) return;
        subscribers = list->second;
    }
    for (const Subscriber& s : subscribers)
        if (!CallGuarded(s.callback, eventId, data, dataSize, s.userData))
            LOG_ERROR("EventBus", "A subscriber to event %u faulted and was skipped", (unsigned)eventId);
}

} // namespace lsproxy
