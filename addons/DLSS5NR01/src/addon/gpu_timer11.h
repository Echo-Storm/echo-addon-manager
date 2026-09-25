// GpuTimer11: GPU time between marks recorded on a D3D11 context, read back a few frames later without a stall (a frame whose queries are not
// back yet is simply not timed). Marks around a queue Wait measure how long the queue sat waiting.
#pragma once
#include <d3d11.h>
#include <cstdint>

class GpuTimer11 {
public:
    static const int kMarks = 4, kRing = 8;
    bool Init(ID3D11Device* dev);
    void Shutdown();
    // One timed stretch: Begin writes mark 0, Mark(i) mark i (in order, 1..kMarks-1), End closes it. Nesting is not allowed.
    void Begin(ID3D11DeviceContext* ctx);
    void Mark(ID3D11DeviceContext* ctx, int i);
    void End(ID3D11DeviceContext* ctx);
    // The average GPU time from mark i-1 to mark i (ms[i], i >= 1; ms[0] is the whole stretch) over the stretches read back since the last call.
    bool Take(double ms[kMarks], uint64_t& stretches);

private:
    void Collect(ID3D11DeviceContext* ctx);
    struct Entry { ID3D11Query* disjoint = nullptr; ID3D11Query* stamp[kMarks] = {}; uint32_t written = 0; bool pending = false; };
    Entry m_ring[kRing];
    int m_next = 0, m_open = -1;
    double m_sum[kMarks] = {}; uint64_t m_count[kMarks] = {}; uint64_t m_stretches = 0;
};
