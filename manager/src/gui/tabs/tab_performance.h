#pragma once

namespace lsproxy {

// Live performance: frame time and model cost from the addons that report them, GPU load / power / clocks from NVML, and a plain-words
// reading of what they say. Data comes from Metrics (IHost::PublishMetric) and GpuStats.
void RenderTabPerformance();

} // namespace lsproxy
