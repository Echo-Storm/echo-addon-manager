#pragma once
#include <string>

struct ImFont;

namespace eam {

void SetupModernStyle();

// Resets the style to the theme and applies the display scale (spacing, rounding, font size).
// Safe to call again when the window moves to a monitor with a different DPI.
void ApplyUiScale(float dpiScale);

// Loads the UI and monospace fonts. Call once after ImGui::CreateContext(), before the first
// frame. Empty paths fall back to Segoe UI / Cascadia Mono / Consolas from the Windows fonts
// folder, then to ImGui's built-in font.
void LoadUiFonts(const std::string& uiFontPath, const std::string& monoFontPath);

// Monospace font for logs and the config editor; nullptr if none could be loaded.
ImFont* MonoFont();
ImFont* TitleFont();   // Segoe UI Semibold (the UI font when it is missing); may be null before LoadUiFonts

// Base font size in logical pixels (before the display scale).
constexpr float kUiFontSize = 15.0f;

} // namespace eam
