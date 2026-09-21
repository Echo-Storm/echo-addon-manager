#pragma once
#include "../../sdk/include/lsproxy/ihost.h"
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace lsproxy {

// The host as an addon sees it: settings, events, logging, Direct3D 11 access, dispatch callbacks, and live status and metrics. Every
// call may come from any thread. Most of it hands straight to the registry that owns the data (settings, events, metrics).
class HostImpl : public IHost {
public:
    HostImpl() = default;
    ~HostImpl() override = default;

    void Log(LsProxyLogLevel level, const char* message) override;

    const char* GetConfig(const char* addonId, const char* key, const char* defaultVal) override;
    void SetConfig(const char* addonId, const char* key, const char* value) override;
    void SaveConfig() override;
    uint32_t GetHostVersion() override;

    void SubscribeEvent(uint32_t eventId, LsProxyEventCallback callback, void* userData) override;
    void UnsubscribeEvent(uint32_t eventId, LsProxyEventCallback callback) override;
    void PublishEvent(uint32_t eventId, const void* data, uint32_t dataSize) override;

    void* GetD3D11Device() override;
    void* GetD3D11DeviceContext() override;
    void SetD3D11Device(void* device, void* context);   // the device capture hook hands these over

    // One callback per owner (the userData): setting one again replaces it, and passing no callback removes it.
    void SetPreDispatchCallback(LsProxyPreDispatchCallback callback, void* userData) override;
    void SetPostDispatchCallback(LsProxyPostDispatchCallback callback, void* userData) override;
    void* GetCurrentComputeShader() override;
    uint32_t GetDispatchCount() override;

    void SetStatus(const char* addonId, const char* text, int level) override;
    void PublishMetric(const char* addonId, const char* key, double value, const char* unit) override;

    // Called from the Dispatch hook. InvokePreDispatch runs every callback and says whether any of them wants the dispatch skipped.
    bool InvokePreDispatch(uint32_t x, uint32_t y, uint32_t z);
    void InvokePostDispatch(uint32_t x, uint32_t y, uint32_t z);

private:
    template <class Callback>
    struct Hook { Callback callback; void* owner; };

    // GetConfig returns a pointer into one of these. The newest kReturnBufferCount stay alive, so a pointer stays valid for that many
    // later calls; nothing is freed on a timer, because another thread may still be reading an older one.
    static constexpr size_t kReturnBufferCount = 512;
    mutable std::mutex m_bufferMutex;
    mutable std::deque<std::string> m_returnBuffers;

    void* m_d3d11Device = nullptr;    // borrowed, not owned
    void* m_d3d11Context = nullptr;

    std::mutex m_dispatchMutex;
    std::vector<Hook<LsProxyPreDispatchCallback>> m_preHooks;
    std::vector<Hook<LsProxyPostDispatchCallback>> m_postHooks;
};

} // namespace lsproxy
