#include "addon/log.h"
#include <eam/addon_sdk.h>
#include <windows.h>
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>

namespace nr {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
IHost* g_host = nullptr;

// Only while the lock can be had at once: the crashing thread may be the one holding it.
void CrashLine(const char* fmt, ...) {
    char text[512]; va_list args; va_start(args, fmt); vsnprintf(text, sizeof text, fmt, args); va_end(args);
    const bool locked = g_mutex.try_lock();
    if (g_file) { fprintf(g_file, "%s\n", text); fflush(g_file); }
    if (locked) g_mutex.unlock();
}

// The stack as module+offset lines; the offsets resolve against the DLSS5NR01.map built beside the DLL.
void CrashReport(const char* what) {
    static std::atomic<bool> reported{ false };
    if (reported.exchange(true)) return;   // terminate() goes on to abort(): once is enough
    CrashLine("CRASH: %s (thread %lu)", what, GetCurrentThreadId());
    void* frames[40] = {};
    const USHORT count = CaptureStackBackTrace(0, 40, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr; wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(frames[i]), &module) && module)
            GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\');
        CrashLine("  #%u %ls+0x%llx", static_cast<unsigned>(i), name ? name + 1 : path, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(module)));
    }
}

void __cdecl OnAbort(int) { CrashReport("abort() called"); }
void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { CrashReport("invalid CRT parameter"); }
void OnTerminate() {
    if (const std::exception_ptr e = std::current_exception()) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& ex) { CrashLine("CRASH: uncaught C++ exception: %s", ex.what()); }
        catch (...) { CrashLine("CRASH: uncaught non-standard exception"); }
    }
    CrashReport("std::terminate");
    abort();
}
} // namespace

void OpenLog(const std::wstring& lsDir, IHost* host) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_host = host;
    CreateDirectoryW((lsDir + L"\\logs").c_str(), nullptr);
    std::wstring path = lsDir + L"\\logs\\DLSS5NR01.log";
    // A second copy of Lossless Scaling that is starting up finds the file open: it writes beside it instead of wiping the running session's.
    if (!MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING) && GetLastError() != ERROR_FILE_NOT_FOUND)
        path = lsDir + L"\\logs\\DLSS5NR01-" + std::to_wstring(GetCurrentProcessId()) + L".log";
    g_file = _wfopen(path.c_str(), L"w");
}

void CloseLog() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) { fclose(g_file); g_file = nullptr; }
    g_host = nullptr;
}

void Log(const char* fmt, ...) {
    char text[1024]; va_list args; va_start(args, fmt); vsnprintf(text, sizeof text, fmt, args); va_end(args);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_host) g_host->Log(EAM_LOG_INFO, text);
    if (g_file) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_file, "[%02d:%02d:%02d.%03d] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
        fflush(g_file);
    }
}

void InstallCrashReports() {
    std::set_terminate(OnTerminate);
    std::signal(SIGABRT, OnAbort);
    _set_invalid_parameter_handler(OnInvalidParameter);
}

} // namespace nr
