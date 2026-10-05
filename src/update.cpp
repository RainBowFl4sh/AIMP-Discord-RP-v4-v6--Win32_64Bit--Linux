#include "update.h"

#include <mutex>
#include <vector>

#include "config.h"
#include "i18n.h"
#include "jobs.h"
#include "util.h"
#include "version.h"
#include "web.h"

namespace update {
namespace {

struct Release {
    std::wstring version, asset;   // asset: download URL of the .aimppack
    std::string  digest;           // "sha256:<hex>" (GitHub), may be empty
};

std::mutex   g_mu;
State        g_state;
Release      g_release;            // result of the last successful check
std::wstring g_notify, g_notified;
std::wstring g_pluginsDir;         // AIMP's plugin folder
bool         g_restart = false;    // the update is installed: the main thread restarts AIMP
bool         g_checkedThisSession = false;   // jobs thread only
bool         g_cleaned = false;              // jobs thread only

// After the package was handed to AIMP: the plugin files AIMP replaces when it installs it (jobs thread only)
struct Watch {
    std::vector<std::wstring> files;
    std::vector<uint64_t>     before, last;   // stamps before the hand-over / at the last look
    std::wstring              version;
    uint64_t                  until = 0;      // TickMs when we stop waiting; 0 = nothing to watch
} g_watch;

const wchar_t* const kCurrent = AIMP_DISCORD_RPC_VERSION_W;

void SetState(Phase p, const std::wstring& version = std::wstring(), const std::wstring& detail = std::wstring()) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_state = State{p, version, detail};
    }
    NotifyUi();
}

std::wstring HttpError(int status) {
    return status ? util::Subst(i18n::T("Upd.Http"), std::to_wstring(status)) : i18n::T("Upd.Offline");
}

bool FetchRelease(Release& rel, std::wstring& err) {
    web::Response r = web::Request(L"GET", L"https://api.github.com/repos/" + util::FromUtf8(AIMP_DISCORD_RPC_REPO) +
                                               L"/releases/latest",
                                   {L"Accept: application/vnd.github+json"}, std::string(), 2u << 20);
    if (r.status == 404) {
        err = i18n::T("Upd.NoRelease");
        return false;
    }
    if (r.status != 200) {
        err = HttpError(r.status);
        return false;
    }
    // the version: from the tag ("v1.5.0"), else the release title ("v1.3r"), else the release notes
    const std::string head = r.body.substr(0, r.body.find("\"assets\""));
    std::wstring v = util::FindVersion(util::FromUtf8(util::JsonGetString(r.body, "tag_name")));
    if (v.empty()) v = util::FindVersion(util::FromUtf8(util::JsonGetString(head, "name")));
    if (v.empty()) v = util::FindVersion(util::FromUtf8(util::JsonGetString(r.body, "body")));
    if (v.empty()) {
        err = i18n::T("Upd.NoVersion");
        return false;
    }
    rel = Release();
    rel.version = v;
    // always the .aimppack of the release (AIMP installs it), straight from this project on GitHub; with several
    // packages the one named after the plugin wins. Other files (zips, single DLLs) are never used for updates.
    bool named = false;
    for (const std::string& a : util::JsonObjects(r.body, "assets")) {
        const std::wstring name = util::Lower(util::FromUtf8(util::JsonGetString(a, "name")));
        const std::string url = util::JsonGetString(a, "browser_download_url");
        if (util::FileExt(name) != L"aimppack" || util::FileExt(util::Lower(util::FromUtf8(url))) != L"aimppack" ||
            url.rfind("https://github.com/RainBowFl4sh/", 0) != 0)
            continue;
        const bool ours = name.find(L"aimp_discord_rpc") != std::wstring::npos;
        if (!rel.asset.empty() && (named || !ours)) continue;
        rel.asset = util::FromUtf8(url);
        rel.digest = util::JsonGetString(a, "digest");
        named = ours;
    }
    return true;
}

void DownloadAndOpen(const Release& rel) {
    if (rel.asset.empty()) {
        SetState(Phase::Failed, rel.version, i18n::T("Upd.NoPackage"));
        return;
    }
    SetState(Phase::Downloading, rel.version);
    web::Response r = web::Get(rel.asset, 64u << 20);
    if (r.status != 200 || r.body.size() < 1000 || r.body.compare(0, 4, "PK\x03\x04") != 0) {
        SetState(Phase::Failed, rel.version, r.status == 200 ? i18n::T("Upd.Damaged") : HttpError(r.status));
        return;
    }
    // the checksum GitHub publishes for the file, and our own file names inside the ZIP
    if ((!rel.digest.empty() && rel.digest != "sha256:" + util::Sha256Hex(r.body)) ||
        r.body.find("aimp_discord_rpc") == std::string::npos) {
        util::Log(L"Update package rejected: checksum mismatch");
        SetState(Phase::Failed, rel.version, i18n::T("Upd.Damaged"));
        return;
    }
    const std::wstring path = config::CacheDir() + util::kPathSep + L"aimp_discord_rpc-" + rel.version + L".aimppack";
    if (!util::WriteFileBytes(path, r.body)) {
        SetState(Phase::Failed, rel.version, util::Subst(i18n::T("Upd.WriteFailed"), path));
        return;
    }
    config::Update([&](Config& c) { c.updateOffered = rel.version; });
    util::Log(L"Update %ls downloaded - opening it in AIMP", rel.version.c_str());
    // AIMP replaces the plugin file (the loaded one, or the copy in its plugin folder) when it installs the package
    Watch w;
    w.version = rel.version;
    const std::wstring module = util::ModuleFile();
    if (!module.empty()) w.files.push_back(module);
    std::wstring dir;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        dir = g_pluginsDir;
    }
    if (!dir.empty() && !module.empty()) {
        const std::wstring sep(1, util::kPathSep);
        const std::wstring copy = dir + sep + L"aimp_discord_rpc" + sep + module.substr(util::DirName(module).size() + 1);
        if (util::Lower(copy) != util::Lower(module)) w.files.push_back(copy);
    }
    for (const std::wstring& f : w.files) w.before.push_back(util::FileStamp(f));
    w.last = w.before;
    // AIMP installs packages that are opened with it (like a double click on the file)
    if (!util::Launch(util::HostExe(), path)) {
        SetState(Phase::Failed, rel.version, util::Subst(i18n::T("Upd.OpenFailed"), path));
        return;
    }
    w.until = util::TickMs() + 15 * 60 * 1000;   // AIMP may ask first
    g_watch = w;
    SetState(Phase::Opened, rel.version, path);
}

void CheckSync(bool automatic) {
    SetState(Phase::Checking);
    Release rel;
    std::wstring err;
    if (!FetchRelease(rel, err)) {
        util::Log(L"Update check failed: %ls", err.c_str());
        SetState(Phase::Failed, std::wstring(), err);
        return;
    }
    const int64_t now = util::UnixTime();
    config::Update([&](Config& c) {
        c.updateLastCheck = now;
        c.updateLatest = rel.version;
    });
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_release = rel;
    }
    if (util::CompareVersions(rel.version, kCurrent) <= 0) {
        SetState(Phase::UpToDate, rel.version);
        return;
    }
    util::Log(L"Update available: %ls", rel.version.c_str());
    SetState(Phase::Available, rel.version);
    if (!automatic) return;
    const Config c = config::Get();
    if (c.updateAuto && c.updateOffered != rel.version) {   // install automatically, once per version
        DownloadAndOpen(rel);
        return;
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_notified != rel.version) {
        g_notified = rel.version;
        g_notify = util::Subst(i18n::T("Upd.Notify"), rel.version);
    }
}

}  // namespace

State Get() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_state;
}

void SetPluginsDir(const std::wstring& dir) {
    std::wstring d = dir;
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    std::lock_guard<std::mutex> lk(g_mu);
    g_pluginsDir = d;
}

uint64_t WatchInstall() {
    if (!g_watch.until) return 0;
    // installed: a plugin file was replaced and has not changed since the last look (AIMP finished writing it)
    bool changed = false, settled = true;
    for (size_t i = 0; i < g_watch.files.size(); ++i) {
        const uint64_t stamp = util::FileStamp(g_watch.files[i]);
        if (stamp && stamp != g_watch.before[i]) {
            changed = true;
            if (stamp != g_watch.last[i]) settled = false;
        }
        g_watch.last[i] = stamp;
    }
    if (changed && settled) {
        const std::wstring version = g_watch.version;
        g_watch = Watch();
        util::Log(L"AIMP installed version %ls - restarting AIMP to load it", version.c_str());
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_restart = true;
        }
        SetState(Phase::Restarting, version);   // wakes the main thread, which restarts AIMP
        return 0;
    }
    if (util::TickMs() >= g_watch.until) {
        util::Log(L"AIMP did not install version %ls - not restarting", g_watch.version.c_str());
        g_watch = Watch();
        return 0;
    }
    return 1000;
}

bool TakeRestart() {
    std::lock_guard<std::mutex> lk(g_mu);
    const bool r = g_restart;
    g_restart = false;
    return r;
}

void RestartFailed() {
    std::wstring version;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        version = g_state.version;
        g_notify = util::Subst(i18n::T("Upd.RestartManual"), version);   // also in AIMP's display
    }
    SetState(Phase::Installed, version);
}

void Check() {
    jobs::Post([] { CheckSync(false); });
}

void Install() {
    jobs::Post([] {
        Release rel;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            rel = g_release;
        }
        if (rel.asset.empty()) {   // not checked in this session yet
            std::wstring err;
            SetState(Phase::Checking);
            if (!FetchRelease(rel, err)) {
                SetState(Phase::Failed, std::wstring(), err);
                return;
            }
        }
        if (util::CompareVersions(rel.version, kCurrent) <= 0) {
            SetState(Phase::UpToDate, rel.version);
            return;
        }
        DownloadAndOpen(rel);
    });
}

void Tick() {
    if (!g_cleaned) {   // packages of versions that are installed now are not needed any more
        g_cleaned = true;
        // first start after AIMP installed a new version (and restarted): say so once
        const std::wstring last = config::Get().lastVersion;
        if (last != kCurrent) {
            config::Update([](Config& c) { c.lastVersion = kCurrent; });
            if (!last.empty() && util::CompareVersions(kCurrent, last) > 0) {
                util::Log(L"Updated from %ls to %ls", last.c_str(), kCurrent);
                std::lock_guard<std::mutex> lk(g_mu);
                g_notify = util::Subst(i18n::T("Upd.Installed"), kCurrent);
            }
        }
        const std::wstring dir = config::CacheDir();
        for (const std::wstring& name : util::ListFiles(dir, L".aimppack"))
            if (util::CompareVersions(util::FindVersion(name), kCurrent) <= 0) util::RemoveFile(dir + util::kPathSep + name);
    }
    const Config c = config::Get();
    if (!c.updateCheck) return;
    static const int64_t kInterval[] = {0, 86400, 7 * 86400, 30 * 86400};
    const int64_t now = util::UnixTime();
    const bool due = c.updateFrequency == 0 ? !g_checkedThisSession
                                            : (now - c.updateLastCheck >= kInterval[c.updateFrequency] ||
                                               c.updateLastCheck > now);   // clock was set back
    if (!due) return;
    g_checkedThisSession = true;
    CheckSync(true);
}

bool TakeNotification(std::wstring& text) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_notify.empty()) return false;
    text.swap(g_notify);
    g_notify.clear();
    return true;
}

}  // namespace update
