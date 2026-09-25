// A command line for the installer's core. The window (later) uses the same functions.
//   setup_cli detect
//   setup_cli status    <Lossless Scaling folder> [--payload <folder>]
//   setup_cli install   <Lossless Scaling folder> --payload <folder>
//   setup_cli uninstall <Lossless Scaling folder> [--remove-addons]
#include "fileinfo.h"
#include "installer.h"
#include "lsfolder.h"
#include "state.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

using namespace setup;

static const char* SituationName(Situation s) {
    switch (s) {
    case Situation::NotLosslessScaling: return "not a Lossless Scaling folder";
    case Situation::NotInstalled: return "not installed";
    case Situation::Installed: return "installed";
    case Situation::AfterLsUpdate: return "after a Lossless Scaling update (needs repair)";
    case Situation::NoOriginal: return "broken: no Lossless_original.dll";
    case Situation::BothOurs: return "broken: both DLLs are ours";
    case Situation::Unrecognised: return "unrecognised Lossless.dll";
    }
    return "?";
}

static std::wstring Flag(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i + 1 < argc; ++i) if (!wcscmp(argv[i], name)) return argv[i + 1];
    return std::wstring();
}
static bool Has(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i < argc; ++i) if (!wcscmp(argv[i], name)) return true;
    return false;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { puts("usage: setup_cli detect | status <dir> [--payload <dir>] | install <dir> --payload <dir> | uninstall <dir> [--remove-addons]"); return 2; }
    const std::wstring cmd = argv[1];
    if (cmd == L"detect") {
        const auto found = FindCandidates();
        if (found.empty()) puts("no Lossless Scaling folder found; pick one by hand");
        for (const auto& c : found) printf("%s  (%s)\n", Narrow(c.dir).c_str(), c.how.c_str());
        return 0;
    }
    if (argc < 3) { puts("a folder is needed"); return 2; }
    const std::wstring dir = argv[2], payload = Flag(argc, argv, L"--payload");
    if (cmd == L"status") {
        const State s = Inspect(dir);
        const PayloadInfo p = payload.empty() ? PayloadInfo() : CheckPayload(payload);
        const Advice a = Advise(s, p.version);
        printf("folder:     %s\nsituation:  %s\nLossless Scaling: %s   LS Addon Manager: %s   running: %s\n%s\n%s\n", Narrow(dir).c_str(), SituationName(s.situation),
               s.lsVersion.empty() ? "?" : s.lsVersion.c_str(), s.installedVersion.empty() ? "-" : s.installedVersion.c_str(), s.running ? "yes" : "no", a.headline.c_str(), a.detail.c_str());
        if (a.action != Action::None) printf("offer: %s%s\n", a.actionLabel.c_str(), a.blocked ? ("  (blocked: " + a.blockedReason + ")").c_str() : "");
        return 0;
    }
    Result r;
    if (cmd == L"install") {
        if (payload.empty()) { puts("--payload <folder> is needed"); return 2; }
        r = Install(dir, payload);
    } else if (cmd == L"uninstall") {
        r = Uninstall(dir, Has(argc, argv, L"--remove-addons"));
    } else {
        puts("unknown command");
        return 2;
    }
    for (const auto& line : r.log) printf("  %s\n", line.c_str());
    printf("%s\n", r.message.c_str());
    if (!r.backupDir.empty()) printf("backups: %s\n", Narrow(r.backupDir).c_str());
    return r.ok ? 0 : 1;
}
