#include "addon/gpu_timer11.h"

bool GpuTimer11::Init(ID3D11Device* dev) {
    Shutdown();
    D3D11_QUERY_DESC disjoint{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 }, stamp{ D3D11_QUERY_TIMESTAMP, 0 };
    for (Entry& e : m_ring) {
        if (FAILED(dev->CreateQuery(&disjoint, &e.disjoint))) { Shutdown(); return false; }
        for (ID3D11Query*& q : e.stamp) if (FAILED(dev->CreateQuery(&stamp, &q))) { Shutdown(); return false; }
    }
    return true;
}

void GpuTimer11::Shutdown() {
    for (Entry& e : m_ring) {
        if (e.disjoint) e.disjoint->Release();
        for (ID3D11Query* q : e.stamp) if (q) q->Release();
        e = Entry{};
    }
    m_next = 0; m_open = -1;
    for (int i = 0; i < kMarks; ++i) { m_sum[i] = 0; m_count[i] = 0; }
    m_stretches = 0;
}

void GpuTimer11::Collect(ID3D11DeviceContext* ctx) {
    for (Entry& e : m_ring) {
        if (!e.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
        if (ctx->GetData(e.disjoint, &d, sizeof d, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        uint64_t t[kMarks] = {};
        bool back = true;
        for (int i = 0; i < kMarks && back; ++i)
            if ((e.written & (1u << i)) && ctx->GetData(e.stamp[i], &t[i], sizeof t[i], D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) back = false;
        if (!back) continue;
        e.pending = false;
        if (d.Disjoint || !d.Frequency) continue;   // the clock changed meanwhile: not a usable time
        const double msPerTick = 1000.0 / static_cast<double>(d.Frequency);
        int last = 0;
        for (int i = 1; i < kMarks; ++i) {
            if (!(e.written & (1u << i))) continue;
            if (e.written & (1u << (i - 1))) { m_sum[i] += static_cast<double>(t[i] - t[i - 1]) * msPerTick; ++m_count[i]; }
            last = i;
        }
        if (last) { m_sum[0] += static_cast<double>(t[last] - t[0]) * msPerTick; ++m_count[0]; ++m_stretches; }
    }
}

void GpuTimer11::Begin(ID3D11DeviceContext* ctx) {
    m_open = -1;
    if (!m_ring[0].disjoint) return;
    Collect(ctx);
    Entry& e = m_ring[m_next];
    if (e.pending) return;   // still not back from the GPU: this stretch goes untimed rather than wait
    m_open = m_next; m_next = (m_next + 1) % kRing;
    e.written = 1u;
    ctx->Begin(e.disjoint);
    ctx->End(e.stamp[0]);
}

void GpuTimer11::Mark(ID3D11DeviceContext* ctx, int i) {
    if (m_open < 0 || i <= 0 || i >= kMarks) return;
    Entry& e = m_ring[m_open];
    ctx->End(e.stamp[i]);
    e.written |= 1u << i;
}

void GpuTimer11::End(ID3D11DeviceContext* ctx) {
    if (m_open < 0) return;
    Entry& e = m_ring[m_open];
    ctx->End(e.disjoint);
    e.pending = true;
    m_open = -1;
}

bool GpuTimer11::Take(double ms[kMarks], uint64_t& stretches) {
    stretches = m_stretches;
    for (int i = 0; i < kMarks; ++i) { ms[i] = m_count[i] ? m_sum[i] / static_cast<double>(m_count[i]) : 0.0; m_sum[i] = 0; m_count[i] = 0; }
    m_stretches = 0;
    return stretches > 0;
}
