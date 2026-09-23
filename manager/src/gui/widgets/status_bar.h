#pragma once
#include <string>

namespace eam {
namespace widgets {

// The author's Ko-fi page (the same address the other apps use).
inline constexpr const char* kKofiUrl = "https://ko-fi.com/xechostormx";

// Opens the Ko-fi page in the default browser. Only ever called from a click.
void OpenKofi();

// A slim bar along the bottom of the window: `left` in muted text, and a flat "donate <heart> ko-fi" control at the right. Call it at
// the end of the window, after the content child, and reserve StatusBarHeight() for it.
void StatusBar(const std::string& left);
float StatusBarHeight();

} // namespace widgets
} // namespace eam
