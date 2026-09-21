#pragma once
#include <string>

namespace lsproxy {
namespace widgets {

// The system's own file dialogs. They open on the user's screen only when called from a click. Each returns false when cancelled.
bool PickOpenFile(const wchar_t* title, const wchar_t* filterName, const wchar_t* filterSpec, std::wstring& out);
bool PickSaveFile(const wchar_t* title, const wchar_t* defaultName, const wchar_t* filterName, const wchar_t* filterSpec, std::wstring& out);

} // namespace widgets
} // namespace lsproxy
