#include "installer.h"
#include "fileinfo.h"
#include "lsfolder.h"
#include <windows.h>
#include <ctime>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

namespace setup {

namespace {

std::wstring g_stampForTest;

std::wstring Stamp() {
    if (!g_stampForTest.empty()) return g_stampForTest;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[32];
    swprintf(b, 32, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

// A backups folder that does not exist yet: the stamp is only accurate to the second, and a second run in the same second must not write into (and over) the first's.
std::wstring FreshBackupDir(const std::wstring& wanted) {
    if (!Exists(wanted)) return wanted;
    for (int n = 2; n < 1000; ++n) {
        const std::wstring next = wanted + L"-" + std::to_wstring(n);
        if (!Exists(next)) return next;
    }
    return wanted;
}

struct Failure { std::string why; };

// The steps taken so far, each with the way to undo it.
class Journal {
public:
    explicit Journal(std::vector<std::string>& log) : m_log(log) {}
    void Did(const std::string& what, std::function<void()> undo) { m_log.push_back(what); m_undo.push_back(std::move(undo)); }
    void Note(const std::string& what) { m_log.push_back(what); }
    void Rollback() {
        for (auto it = m_undo.rbegin(); it != m_undo.rend(); ++it) { try { (*it)(); } catch (...) {} }
        m_undo.clear();
        m_log.push_back("rolled back: the folder is as it was");
    }
private:
    std::vector<std::string>& m_log;
    std::vector<std::function<void()>> m_undo;
};

bool MakeDirs(const std::wstring& dir) {
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    return !ec || Exists(dir);
}

std::wstring ParentOf(const std::wstring& p) {
    const size_t at = p.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : p.substr(0, at);
}

// A rename that gets a few more tries: antivirus and the search indexer sometimes hold a file for a moment.
bool Move(const std::wstring& from, const std::wstring& to) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED)) return true;
        Sleep(150);
    }
    return false;
}

// Put `src` at `dst`. A different file already there is copied to `backupRoot\rel` first; the new one is written next to it under a temporary name,
// checked by hash, and renamed into place. An identical file is left alone.
void Place(Journal& j, const std::wstring& src, const std::wstring& dst, const std::wstring& backupRoot, const std::wstring& rel) {
    const std::string want = Sha256File(src);
    if (want.empty()) throw Failure{ "could not read " + Narrow(src) };
    const bool had = Exists(dst);
    if (had && Sha256File(dst) == want) { j.Note("already in place: " + Narrow(rel)); return; }
    std::wstring saved;
    if (had) {
        saved = JoinPath(backupRoot, rel);
        if (!MakeDirs(ParentOf(saved)) || !CopyFileW(dst.c_str(), saved.c_str(), FALSE)) throw Failure{ "could not save " + Narrow(rel) + " to the backups" };
    }
    if (!MakeDirs(ParentOf(dst))) throw Failure{ "could not create the folder for " + Narrow(rel) };
    // a read-only file cannot be replaced: lift the flag for the swap (the old file is in the backups; the flag comes back if the swap fails or is rolled back)
    const DWORD oldAttributes = had ? GetFileAttributesW(dst.c_str()) : INVALID_FILE_ATTRIBUTES;
    const bool wasReadOnly = oldAttributes != INVALID_FILE_ATTRIBUTES && (oldAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    const std::wstring tmp = dst + L".setup-new";
    if (!CopyFileW(src.c_str(), tmp.c_str(), FALSE)) throw Failure{ "could not write " + Narrow(rel) };
    if (Sha256File(tmp) != want) { DeleteFileW(tmp.c_str()); throw Failure{ Narrow(rel) + " did not copy correctly" }; }
    if (wasReadOnly) SetFileAttributesW(dst.c_str(), oldAttributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    bool moved = false;
    for (int attempt = 0; attempt < 6 && !moved; ++attempt) {
        moved = MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        if (!moved) Sleep(150);
    }
    if (!moved) {
        DeleteFileW(tmp.c_str());
        if (wasReadOnly) SetFileAttributesW(dst.c_str(), oldAttributes);
        throw Failure{ "could not put " + Narrow(rel) + " in place (is it in use?)" };
    }
    j.Did(had ? "replaced " + Narrow(rel) + " (the old one is in the backups)" : "added " + Narrow(rel),
          [dst, saved, had, wasReadOnly, oldAttributes] {
              if (had) { CopyFileW(saved.c_str(), dst.c_str(), FALSE); if (wasReadOnly) SetFileAttributesW(dst.c_str(), oldAttributes); }
              else DeleteFileW(dst.c_str());
          });
}

void CopyTree(Journal& j, const std::wstring& srcRoot, const std::wstring& dstRoot, const std::wstring& backupRoot, const std::wstring& relRoot) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(fs::path(srcRoot), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::wstring rel = fs::relative(it->path(), fs::path(srcRoot), ec).wstring();
        if (_wcsicmp(it->path().filename().wstring().c_str(), L"config.json") == 0) continue;   // never the person's settings
        Place(j, it->path().wstring(), JoinPath(dstRoot, rel), backupRoot, JoinPath(relRoot, rel));
    }
}

void Rename(Journal& j, const std::wstring& from, const std::wstring& to, const std::string& what) {
    if (!Move(from, to)) throw Failure{ "could not rename " + Narrow(from) + " (is Lossless Scaling closed?)" };
    j.Did(what, [from, to] { MoveFileExW(to.c_str(), from.c_str(), MOVEFILE_COPY_ALLOWED); });
}

Result Fail(Result r, const std::string& why) {
    r.ok = false;
    r.message = why;
    return r;
}

} // namespace

void SetBackupStampForTest(const std::wstring& stamp) { g_stampForTest = stamp; }

PayloadInfo CheckPayload(const std::wstring& payloadDir) {
    PayloadInfo p;
    const std::wstring dll = JoinPath(payloadDir, L"Lossless.dll");
    if (!Exists(dll)) { p.error = "the installer's own Lossless.dll is missing from " + Narrow(payloadDir); return p; }
    const DllInfo info = InspectDll(dll);
    if (info.kind != DllKind::Ours) { p.error = "the Lossless.dll that came with this installer is not Echo Addon Manager's"; return p; }
    p.version = info.version;
    p.ok = true;
    return p;
}

Result Install(const std::wstring& lsDir, const std::wstring& payloadDir) {
    Result r;
    const PayloadInfo payload = CheckPayload(payloadDir);
    if (!payload.ok) return Fail(r, payload.error);
    const State before = Inspect(lsDir);
    const Advice advice = Advise(before, payload.version);
    if (advice.blocked) return Fail(r, advice.blockedReason.empty() ? advice.headline : advice.blockedReason);
    if (before.situation == Situation::Installed && advice.action == Action::None) return Fail(r, advice.headline);

    Journal j(r.log);
    const std::wstring backupDir = FreshBackupDir(JoinPath(lsDir, L"backups\\installer-" + Stamp()));
    const std::wstring lossless = JoinPath(lsDir, L"Lossless.dll"), original = JoinPath(lsDir, L"Lossless_original.dll");
    try {
        if (!MakeDirs(backupDir)) throw Failure{ "could not create the backups folder" };
        r.backupDir = backupDir;

        switch (before.situation) {
        case Situation::NotInstalled:
            if (!CopyFileW(lossless.c_str(), JoinPath(backupDir, L"Lossless.dll").c_str(), FALSE)) throw Failure{ "could not save Lossless Scaling's Lossless.dll to the backups" };
            j.Note("saved Lossless Scaling's Lossless.dll to the backups");
            Rename(j, lossless, original, "renamed Lossless.dll to Lossless_original.dll");
            break;
        case Situation::AfterLsUpdate:
            Rename(j, original, JoinPath(backupDir, L"Lossless_original.dll.old"), "moved the old Lossless_original.dll to the backups");
            Rename(j, lossless, original, "kept the new Lossless Scaling file as Lossless_original.dll");
            break;
        case Situation::Installed:
            break;   // ours is replaced below; the original stays where it is
        default:
            throw Failure{ advice.headline };
        }

        Place(j, JoinPath(payloadDir, L"Lossless.dll"), lossless, backupDir, L"Lossless.dll.ours");
        for (const wchar_t* icon : { L"LP-icon.ico", L"LP-icon.png" })
            if (Exists(JoinPath(payloadDir, icon))) Place(j, JoinPath(payloadDir, icon), JoinPath(lsDir, icon), backupDir, icon);
        if (Exists(JoinPath(payloadDir, L"addons"))) CopyTree(j, JoinPath(payloadDir, L"addons"), JoinPath(lsDir, L"addons"), backupDir, L"addons");

        // a helper DLL an earlier version left behind under its old name
        const std::wstring stale = JoinPath(lsDir, L"addons\\DLSS5NR01\\nvngx.dll_lspnr.dll");
        if (Exists(stale)) Rename(j, stale, JoinPath(backupDir, L"nvngx.dll_lspnr.dll"), "moved the old helper DLL nvngx.dll_lspnr.dll to the backups");

        // is it what was meant?
        const State after = Inspect(lsDir);
        if (after.situation != Situation::Installed || after.installedVersion != payload.version) throw Failure{ "the folder did not end up in the expected state" };
        if (Sha256File(lossless) != Sha256File(JoinPath(payloadDir, L"Lossless.dll"))) throw Failure{ "Lossless.dll did not verify" };
    } catch (const Failure& f) {
        j.Rollback();
        r.rolledBack = true;
        r.ok = false;
        r.message = "Nothing was changed: " + f.why + ".";
        std::error_code ec;
        fs::remove(fs::path(backupDir), ec);   // only succeeds when empty
        return r;
    }

    RememberFolder(lsDir);
    std::error_code ec;
    fs::remove(fs::path(backupDir), ec);       // empty when nothing had to be saved
    if (Exists(backupDir)) r.backupDir = backupDir; else r.backupDir.clear();
    r.ok = true;
    r.message = std::string(before.situation == Situation::Installed ? "Updated to " : (before.situation == Situation::AfterLsUpdate ? "Repaired: Echo Addon Manager " : "Installed Echo Addon Manager ")) + payload.version + ".";
    if (r.message.rfind("Updated to ", 0) == 0) r.message = "Updated Echo Addon Manager to " + payload.version + ".";
    return r;
}

Result Uninstall(const std::wstring& lsDir, bool removeAddons) {
    Result r;
    const State before = Inspect(lsDir);
    if (before.situation == Situation::NotLosslessScaling) return Fail(r, "Lossless Scaling was not found in this folder.");
    if (before.running) return Fail(r, "Close Lossless Scaling first: it keeps these files open.");
    if (before.situation == Situation::NotInstalled) { r.ok = true; r.message = "Echo Addon Manager is not installed here; nothing to do."; return r; }
    if (before.situation != Situation::Installed && before.situation != Situation::AfterLsUpdate) return Fail(r, Advise(before, "").headline);

    Journal j(r.log);
    const std::wstring backupDir = FreshBackupDir(JoinPath(lsDir, L"backups\\uninstall-" + Stamp()));
    const std::wstring lossless = JoinPath(lsDir, L"Lossless.dll"), original = JoinPath(lsDir, L"Lossless_original.dll");
    try {
        if (!MakeDirs(backupDir)) throw Failure{ "could not create the backups folder" };
        r.backupDir = backupDir;
        if (before.situation == Situation::Installed) {
            Rename(j, lossless, JoinPath(backupDir, L"Lossless.dll.ours"), "moved Echo Addon Manager's Lossless.dll to the backups");
            Rename(j, original, lossless, "put Lossless Scaling's own Lossless.dll back");
        } else {   // Lossless Scaling had already put its own back: only the stale copy is left over
            Rename(j, original, JoinPath(backupDir, L"Lossless_original.dll.old"), "moved the stale Lossless_original.dll to the backups");
        }
        if (removeAddons && Exists(JoinPath(lsDir, L"addons"))) Rename(j, JoinPath(lsDir, L"addons"), JoinPath(backupDir, L"addons"), "moved the addons folder to the backups");
        const State after = Inspect(lsDir);
        if (after.situation != Situation::NotInstalled) throw Failure{ "the folder did not end up as a plain Lossless Scaling" };
    } catch (const Failure& f) {
        j.Rollback();
        r.rolledBack = true;
        r.message = "Nothing was changed: " + f.why + ".";
        std::error_code ec;
        fs::remove(fs::path(backupDir), ec);
        return r;
    }
    r.ok = true;
    r.message = removeAddons ? "Uninstalled. Lossless Scaling's own Lossless.dll is back, and the addons folder is in the backups." : "Uninstalled. Lossless Scaling's own Lossless.dll is back; your addons and settings are still in the addons folder.";
    return r;
}

} // namespace setup
