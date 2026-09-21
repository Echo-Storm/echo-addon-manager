#include "host_impl.h"
#include "metrics.h"
#include "../config/config_manager.h"
#include "../core/d3d11_hook.h"
#include "../event/event_system.h"
#include "../log/logger.h"
#include "../../sdk/include/lsproxy/version.h"
#include <algorithm>

namespace lsproxy {

namespace {

// Addons may pass null for any text; the registries want real strings.
const char* Text(const char* s) { return s ? s : ""; }

// Sets, replaces or removes the hook that belongs to `owner`.
template <class Hook, class Callback>
void SetHook(std::vector<Hook>& hooks, Callback callback, void* owner) {
    const auto mine = std::find_if(hooks.begin(), hooks.end(), [owner](const Hook& h) { return h.owner == owner; });
    if (!callback) {
        if (mine != hooks.end()) hooks.erase(mine);
    } else if (mine != hooks.end()) {
        mine->callback = callback;
    } else {
        hooks.push_back({ callback, owner });
    }
}

} // namespace

void HostImpl::Log(LsProxyLogLevel level, const char* message) {
    Logger::Instance().Log(static_cast<LogLevel>(level), "Addon", "%s", Text(message));
}

// ---- settings

const char* HostImpl::GetConfig(const char* addonId, const char* key, const char* defaultVal) {
    std::string value = ConfigManager::Instance().Get(Text(addonId), Text(key), Text(defaultVal));
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_returnBuffers.push_back(std::move(value));
    if (m_returnBuffers.size() > kReturnBufferCount) m_returnBuffers.pop_front();
    return m_returnBuffers.back().c_str();
}

void HostImpl::SetConfig(const char* addonId, const char* key, const char* value) {
    ConfigManager::Instance().Set(Text(addonId), Text(key), Text(value));
}

void HostImpl::SaveConfig() { ConfigManager::Instance().Save(); }

uint32_t HostImpl::GetHostVersion() { return LSPROXY_API_VERSION_INT; }

// ---- events

void HostImpl::SubscribeEvent(uint32_t eventId, LsProxyEventCallback callback, void* userData) {
    EventBus::Instance().Subscribe(eventId, callback, userData);
}

void HostImpl::UnsubscribeEvent(uint32_t eventId, LsProxyEventCallback callback) {
    EventBus::Instance().Unsubscribe(eventId, callback);
}

void HostImpl::PublishEvent(uint32_t eventId, const void* data, uint32_t dataSize) {
    EventBus::Instance().Publish(eventId, data, dataSize);
}

// ---- Direct3D 11

void* HostImpl::GetD3D11Device() { return m_d3d11Device; }
void* HostImpl::GetD3D11DeviceContext() { return m_d3d11Context; }

void HostImpl::SetD3D11Device(void* device, void* context) {
    m_d3d11Device = device;
    m_d3d11Context = context;
}

void* HostImpl::GetCurrentComputeShader() { return D3D11Hook::GetCurrentComputeShader(); }
uint32_t HostImpl::GetDispatchCount() { return D3D11Hook::GetDispatchCount(); }

// ---- dispatch callbacks

void HostImpl::SetPreDispatchCallback(LsProxyPreDispatchCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    SetHook(m_preHooks, callback, userData);
}

void HostImpl::SetPostDispatchCallback(LsProxyPostDispatchCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    SetHook(m_postHooks, callback, userData);
}

bool HostImpl::InvokePreDispatch(uint32_t x, uint32_t y, uint32_t z) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    bool skip = false;
    for (const auto& hook : m_preHooks)
        if (hook.callback(x, y, z, hook.owner)) skip = true;   // all of them run, even after one asks to skip
    return skip;
}

void HostImpl::InvokePostDispatch(uint32_t x, uint32_t y, uint32_t z) {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    for (const auto& hook : m_postHooks) hook.callback(x, y, z, hook.owner);
}

// ---- live status and metrics

void HostImpl::SetStatus(const char* addonId, const char* text, int level) {
    Metrics::Instance().SetStatus(Text(addonId), Text(text), level);
}

void HostImpl::PublishMetric(const char* addonId, const char* key, double value, const char* unit) {
    Metrics::Instance().Publish(Text(addonId), Text(key), value, Text(unit));
}

} // namespace lsproxy
