#pragma once
#include "../../sdk/include/lsproxy/ihost.h"
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace lsproxy {

class HostImpl : public IHost {
public:
    HostImpl();
    ~HostImpl() override = default;

    void Log(LsProxyLogLevel level, const char* message) override;
    const char* GetConfig(const char* addonId, const char* key, const char* defaultVal) override;
    void SetConfig(const char* addonId, const char* key, const char* value) override;
    void SaveConfig() override;
    uint32_t GetHostVersion() override;
    void SubscribeEvent(uint32_t eventId, LsProxyEventCallback callback, void* userData) override;
    void UnsubscribeEvent(uint32_t eventId, LsProxyEventCallback callback) override;
    void PublishEvent(uint32_t eventId, const void* data, uint32_t dataSize) override;

    // D3D11 device access
    void* GetD3D11Device() override;
    void* GetD3D11DeviceContext() override;

    // Dispatch hooks
    void SetPreDispatchCallback(LsProxyPreDispatchCallback callback, void* userData) override;
    void SetPostDispatchCallback(LsProxyPostDispatchCallback callback, void* userData) override;

    // Internal: set by the device capture hook
    void SetD3D11Device(void* device, void* context);

    // D3D11 state queries
    void* GetCurrentComputeShader() override;
    uint32_t GetDispatchCount() override;

    // Live status and metrics (host 0.5.0)
    void SetStatus(const char* addonId, const char* text, int level) override;
    void PublishMetric(const char* addonId, const char* key, double value, const char* unit) override;

    // Internal: called from the Dispatch vtable hook
    bool InvokePreDispatch(uint32_t x, uint32_t y, uint32_t z);
    void InvokePostDispatch(uint32_t x, uint32_t y, uint32_t z);

private:
    // Ring of return buffers: each GetConfig result stays valid until kReturnBufferCount more
    // calls have been made (deque keeps the other elements in place when the oldest is dropped).
    static constexpr size_t kReturnBufferCount = 512;
    mutable std::mutex m_bufferMutex;
    mutable std::deque<std::string> m_returnBuffers;

    // D3D11 device pointers (borrowed, not owned)
    void* m_d3d11Device = nullptr;
    void* m_d3d11Context = nullptr;

    // Dispatch hook callbacks (multi-subscriber)
    struct PreDispatchEntry {
        LsProxyPreDispatchCallback callback;
        void* userData;
    };
    struct PostDispatchEntry {
        LsProxyPostDispatchCallback callback;
        void* userData;
    };
    std::mutex m_dispatchMutex;
    std::vector<PreDispatchEntry> m_preDispatchers;
    std::vector<PostDispatchEntry> m_postDispatchers;
};

} // namespace lsproxy
