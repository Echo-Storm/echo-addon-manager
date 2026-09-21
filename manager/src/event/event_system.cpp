#include "event_system.h"
#include "../log/logger.h"
#include <windows.h>

namespace lsproxy {

// SEH wrapper - must be separate from functions with C++ destructors
static void SehCallEventCallback(EventCallback cb, uint32_t eventId,
                                 const void* data, uint32_t dataSize, void* userData) {
    __try {
        cb(eventId, data, dataSize, userData);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Logged by caller
    }
}

EventBus& EventBus::Instance() {
    static EventBus instance;
    return instance;
}

void EventBus::Subscribe(uint32_t eventId, EventCallback callback, void* userData) {
    if (!callback) return;
    std::unique_lock lock(m_mutex);
    m_subscribers[eventId].push_back({callback, userData});
}

void EventBus::Unsubscribe(uint32_t eventId, EventCallback callback) {
    std::unique_lock lock(m_mutex);
    auto it = m_subscribers.find(eventId);
    if (it == m_subscribers.end()) return;

    auto& subs = it->second;
    subs.erase(
        std::remove_if(subs.begin(), subs.end(),
            [callback](const Subscriber& s) { return s.callback == callback; }),
        subs.end());
}

void EventBus::Publish(uint32_t eventId, const void* data, uint32_t dataSize) {
    std::vector<Subscriber> snapshot;
    {
        std::shared_lock lock(m_mutex);
        auto it = m_subscribers.find(eventId);
        if (it == m_subscribers.end()) return;
        snapshot = it->second;
    }

    for (const auto& sub : snapshot) {
        SehCallEventCallback(sub.callback, eventId, data, dataSize, sub.userData);
    }
}

} // namespace lsproxy
