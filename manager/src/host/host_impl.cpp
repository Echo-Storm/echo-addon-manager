#include "host_impl.h"
#include "metrics.h"
#include <algorithm>
#include "../config/config_manager.h"
#include "../event/event_system.h"
#include "../log/logger.h"
#include "../core/d3d11_hook.h"
#include "../../sdk/include/lsproxy/version.h"

namespace lsproxy {

HostImpl::HostImpl() {}

void HostImpl::Log(LsProxyLogLevel level, const char* message) {
    Logger::Instance().Log(static_cast<LogLevel>(level), "Addon", "%s", message);
}

const char* HostImpl::GetConfig(const char* addonId, const char* key, const char* defaultVal) {
    std::string value = ConfigManager::Instance().Get(
        addonId ? addonId : "", key ? key : "", defaultVal ? defaultVal : "");
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_returnBuffers.push_back(std::move(value));
    // Keep only the most recent strings. Nothing else frees these, and clearing them on a timer
    // (as this used to do once per UI frame) frees a string another thread may still be reading.
    if (m_returnBuffers.size() > kReturnBufferCount) m_returnBuffers.pop_front();
    return m_returnBuffers.back().c_str();
}

void HostImpl::SetConfig(const char* addonId, const char* key, const char* value) {
    ConfigManager::Instance().Set(
        addonId ? addonId : "", key ? key : "", value ? value : "");
}

void HostImpl::SaveConfig() {
    ConfigManager::Instance().Save();
}

void HostImpl::SetStatus(const char* addonId, const char* text, int level) {
    Metrics::Instance().SetStatus(addonId ? addonId : "", text ? text : "", level);
}

void HostImpl::PublishMetric(const char* addonId, const char* key, double value, const char* unit) {
    Metrics::Instance().Publish(addonId ? addonId : "", key ? key : "", value, unit ? unit : "");
}

uint32_t HostImpl::GetHostVersion() {
    return LSPROXY_API_VERSION_INT;
}

void HostImpl::SubscribeEvent(uint32_t eventId, LsProxyEventCallback callback, void* userData) {
    EventBus::Instance().Subscribe(eventId, callback, userData);
}

void HostImpl::UnsubscribeEvent(uint32_t eventId, LsProxyEventCallback callback) {
    EventBus::Instance().Unsubscribe(eventId, callback);
}

void HostImpl::PublishEvent(uint32_t eventId, const void* data, uint32_t dataSize) {
    EventBus::Instance().Publish(eventId, data, dataSize);
}

void* HostImpl::GetD3D11Device() {
    return m_d3d11Device;
}

void* HostImpl::GetD3D11DeviceContext() {
    return m_d3d11Context;
}

void HostImpl::SetD3D11Device(void* device, void* context) {
    m_d3d11Device = device;
    m_d3d11Context = context;
}

void HostImpl::SetPreDispatchCallback(LsProxyPreDispatchCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    if (!callback) {
        // Remove entries matching this userData
        m_preDispatchers.erase(
            std::remove_if(m_preDispatchers.begin(), m_preDispatchers.end(),
                [userData](const PreDispatchEntry& e) { return e.userData == userData; }),
            m_preDispatchers.end());
        return;
    }
    // Replace if same userData exists, otherwise add
    for (auto& e : m_preDispatchers) {
        if (e.userData == userData) { e.callback = callback; return; }
    }
    m_preDispatchers.push_back({callback, userData});
}

void HostImpl::SetPostDispatchCallback(LsProxyPostDispatchCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    if (!callback) {
        m_postDispatchers.erase(
            std::remove_if(m_postDispatchers.begin(), m_postDispatchers.end(),
                [userData](const PostDispatchEntry& e) { return e.userData == userData; }),
            m_postDispatchers.end());
        return;
    }
    for (auto& e : m_postDispatchers) {
        if (e.userData == userData) { e.callback = callback; return; }
    }
    m_postDispatchers.push_back({callback, userData});
}

void* HostImpl::GetCurrentComputeShader() {
    return D3D11Hook::GetCurrentComputeShader();
}

uint32_t HostImpl::GetDispatchCount() {
    return D3D11Hook::GetDispatchCount();
}

bool HostImpl::InvokePreDispatch(uint32_t x, uint32_t y, uint32_t z) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    bool skip = false;
    for (auto& e : m_preDispatchers) {
        if (e.callback && e.callback(x, y, z, e.userData)) skip = true;
    }
    return skip;
}

void HostImpl::InvokePostDispatch(uint32_t x, uint32_t y, uint32_t z) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    for (auto& e : m_postDispatchers) {
        if (e.callback) e.callback(x, y, z, e.userData);
    }
}

} // namespace lsproxy
