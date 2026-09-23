#pragma once

namespace eam {

// UI scale = monitor DPI / 96. Sizes written in widget code are "logical" pixels; S() turns
// them into physical pixels so layout follows the display scale, not just the font.
float UiScale();
void SetUiScale(float scale);

inline float S(float logicalPx) { return logicalPx * UiScale(); }

} // namespace eam
