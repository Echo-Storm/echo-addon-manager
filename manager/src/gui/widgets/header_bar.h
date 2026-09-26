#pragma once

namespace eam {
namespace widgets {

// The band across the top of the window, on every tab: the logo, the product name and version at the left; at the right a live readout
// of the machine (graphics card load and memory, system memory, processor), so its state is seen at a glance. The memory figures turn
// amber, then red, as they fill. Hovering the readout shows the details (card name, temperature, power, clocks).
float HeaderBarHeight();
void HeaderBar();

} // namespace widgets
} // namespace eam
