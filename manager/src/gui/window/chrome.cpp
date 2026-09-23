#include "chrome.h"
#include "../../../third_party/stb_image.h"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace eam {
namespace window {

void ApplyDarkTitleBar(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using SetAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (const auto set = reinterpret_cast<SetAttributeFn>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) {
        constexpr DWORD kUseImmersiveDarkMode = 20, kBorderColor = 34, kCaptionColor = 35, kTextColor = 36;
        const BOOL dark = TRUE;
        const COLORREF caption = RGB(0x18, 0x18, 0x18), text = RGB(0xe8, 0xe8, 0xe8), border = RGB(0x2a, 0x2a, 0x2a);
        set(hwnd, kUseImmersiveDarkMode, &dark, sizeof dark);
        set(hwnd, kCaptionColor, &caption, sizeof caption);
        set(hwnd, kTextColor, &text, sizeof text);
        set(hwnd, kBorderColor, &border, sizeof border);
    }
    FreeLibrary(dwm);
}

namespace {

HICON IconFromIco(const fs::path& path, int cx, int cy) {
    return static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE));
}

// A PNG file as an icon: decoded with stb_image, red and blue swapped into the BGRA order Windows wants.
HICON IconFromPng(const fs::path& path) {
    const std::string utf8 = path.u8string();
    int w = 0, h = 0, channels = 0;
    unsigned char* rgba = stbi_load(utf8.c_str(), &w, &h, &channels, 4);
    if (!rgba) return nullptr;
    for (int i = 0; i < w * h; ++i) std::swap(rgba[i * 4 + 0], rgba[i * 4 + 2]);

    HBITMAP color = CreateBitmap(w, h, 1, 32, rgba);
    stbi_image_free(rgba);
    if (!color) return nullptr;

    HDC screen = GetDC(nullptr);
    HBITMAP mask = CreateCompatibleBitmap(screen, w, h);
    ReleaseDC(nullptr, screen);
    if (!mask) { DeleteObject(color); return nullptr; }

    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

fs::path Beside(HMODULE module, const wchar_t* file) {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(module, p, MAX_PATH);
    return fs::path(p).parent_path() / file;
}

} // namespace

AppIcons LoadAppIcons() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&LoadAppIcons), &self);

    std::error_code ec;
    const fs::path icoBesideExe = Beside(nullptr, L"manager-icon.ico"), icoBesideDll = Beside(self, L"manager-icon.ico");
    const fs::path pngBesideExe = Beside(nullptr, L"manager-icon.png"), pngBesideDll = Beside(self, L"manager-icon.png");

    AppIcons icons;
    const fs::path* ico = fs::exists(icoBesideExe, ec) ? &icoBesideExe : (fs::exists(icoBesideDll, ec) ? &icoBesideDll : nullptr);
    if (ico) {
        icons.big = IconFromIco(*ico, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
        icons.little = IconFromIco(*ico, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    }
    if (!icons.big) {
        if (fs::exists(pngBesideExe, ec)) icons.big = IconFromPng(pngBesideExe);
        else if (fs::exists(pngBesideDll, ec)) icons.big = IconFromPng(pngBesideDll);
    }
    if (!icons.little) icons.little = icons.big;
    return icons;
}

} // namespace window
} // namespace eam
