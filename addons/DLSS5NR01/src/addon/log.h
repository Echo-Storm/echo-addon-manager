// The addon's log: <Lossless Scaling>\logs\DLSS5NR01.log, begun afresh at every start (the one before is kept as .old), each line also
// sent to the manager's log. And the crash reports: this DLL has its own copy of the C runtime, so the manager's handlers never see a failure
// that starts here; these write down what happened, and where, before the process goes.
#pragma once
#include <string>

struct IHost;

namespace nr {

void OpenLog(const std::wstring& lsDir, IHost* host);
void CloseLog();
void Log(const char* fmt, ...);
void InstallCrashReports();   // std::terminate, abort() and invalid CRT parameters

} // namespace nr
