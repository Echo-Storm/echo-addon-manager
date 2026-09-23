#pragma once
#include "imgui.h"
#include <string>

namespace eam {
namespace widgets {

enum class ToastType { Info, Success, Warning, Error };

void ToastShow(const std::string& message, ToastType type = ToastType::Info, float duration = 3.0f);
void ToastRender(); // Call once per frame at the end of rendering

} // namespace widgets
} // namespace eam
