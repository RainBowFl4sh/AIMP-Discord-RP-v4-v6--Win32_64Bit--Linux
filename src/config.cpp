#include "config.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <cwchar>
#include <fstream>
#include <map>
#endif

#include <mutex>

#include "util.h"

namespace {

std::mutex g_mutex;
Config     g_cfg;

const wchar_t* kSection = L"DiscordRPC";

std::wstring IniPath() { return config::DataDir() + util::kPathSep + L"DiscordRPC.ini"; }

#ifdef _WIN32

void BeginRead() {}
void Flush() {}

std::wstring ReadStr(const wchar_t* key, const std::wstring& def) {
    std::wstring buf(4096, L'\0');
    DWORD n = GetPrivateProfileStringW(kSection, key, def.c_str(), &buf[0], (DWORD)buf.size(), IniPath().c_str());
    buf.resize(n);
    return buf;
}
int ReadInt(const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(kSection, key, def, IniPath().c_str());
}
void WriteStr(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(kSection, key, v.c_str(), IniPath().c_str());
}

#else  // Linux: small INI reader / writer with the same format (UTF-8, one [DiscordRPC] section)

std::map<std::wstring, std::wstring> g_ini;   // guarded by the callers (Load / Set run one at a time)
std::mutex g_iniMutex;
uint64_t   g_iniMtime = 0;

uint64_t FileMtime() {   // modification time (ns) mixed with the size, 0 = no file
    struct stat st;
    if (stat(util::ToUtf8(IniPath()).c_str(), &st) != 0) return 0;
    return ((uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec) ^ ((uint64_t)st.st_size << 48);
}

void BeginRead() {
    g_ini.clear();
    std::ifstream in(util::ToUtf8(IniPath()), std::ios::binary);
    std::string line;
    bool inSection = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::wstring l = util::Trim(util::FromUtf8(line));
        if (l.empty() || l[0] == L';' || l[0] == L'#') continue;
        if (l[0] == L'[') {
            inSection = util::Lower(l) == util::Lower(std::wstring(L"[") + kSection + L"]");
            continue;
        }
        size_t eq = l.find(L'=');
        if (!inSection || eq == std::wstring::npos) continue;
        std::wstring v = util::Trim(l.substr(eq + 1));
        if (v.size() >= 2 && (v[0] == L'"' || v[0] == L'\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
        g_ini[util::Lower(util::Trim(l.substr(0, eq)))] = v;
    }
    g_iniMtime = FileMtime();
}

void Flush() {
    std::string out = "[" + util::ToUtf8(kSection) + "]\n";
    for (const auto& kv : g_ini) out += util::ToUtf8(kv.first) + "=" + util::ToUtf8(kv.second) + "\n";
    std::string path = util::ToUtf8(IniPath()), tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << out;
    }
    rename(tmp.c_str(), path.c_str());
    g_iniMtime = FileMtime();
}

std::wstring ReadStr(const wchar_t* key, const std::wstring& def) {
    auto it = g_ini.find(util::Lower(key));
    return it == g_ini.end() ? def : it->second;
}
int ReadInt(const wchar_t* key, int def) {
    auto it = g_ini.find(util::Lower(key));
    if (it == g_ini.end()) return def;
    const wchar_t* p = it->second.c_str();
    wchar_t* end = nullptr;
    long v = wcstol(p, &end, 10);
    return end == p ? def : (int)v;
}
void WriteStr(const wchar_t* key, const std::wstring& v) {
    // keys are stored lower case; values stay on one line
    std::wstring clean = util::ReplaceAll(util::ReplaceAll(v, L"\r", L" "), L"\n", L" ");
    g_ini[util::Lower(key)] = clean;
}

#endif

void WriteStuff(const Config& c);   // writes every option (no flush)

bool ReadBool(const wchar_t* key, bool def) { return ReadInt(key, def ? 1 : 0) != 0; }
void WriteInt(const wchar_t* key, int v) { WriteStr(key, std::to_wstring(v)); }
void WriteBool(const wchar_t* key, bool v) { WriteInt(key, v ? 1 : 0); }

}  // namespace

namespace config {

std::wstring DataDir() {
#ifdef _WIN32
    std::wstring appdata = util::GetEnv(L"APPDATA");
    std::wstring dir = appdata.empty() ? std::wstring(L".") : appdata + L"\\AIMP";
#else
    // $XDG_CONFIG_HOME/AIMP (usually ~/.config/AIMP)
    std::wstring base = util::GetEnv(L"XDG_CONFIG_HOME");
    if (base.empty()) {
        std::wstring home = util::GetEnv(L"HOME");
        if (home.empty()) return L".";
        base = home + L"/.config";
        util::MakeDir(base);
    }
    std::wstring dir = base + L"/AIMP";
#endif
    util::MakeDir(dir);
    return dir;
}

// fills the computed fields (effective client ID / title link) from the user-facing options
static void Resolve(Config& c) {
    c.clientId = (c.useCustomApp && !c.customClientId.empty()) ? c.customClientId : std::wstring(kDefaultClientId);
    if (!c.titleLink)                                   c.detailsUrl.clear();
    else if (c.titleLinkCustom && !c.titleLinkUrl.empty()) c.detailsUrl = c.titleLinkUrl;
    else                                                c.detailsUrl = kDefaultTitleLink;
    c.stateUrl.clear();
    // fixed asset keys / no buttons: these are not user settings any more
    c.playKey = L"play"; c.pauseKey = L"pause"; c.fallbackKey = L"aimp";
    c.btn1Enabled = c.btn2Enabled = false;
}

void Load() {
#ifndef _WIN32
    std::lock_guard<std::mutex> ini(g_iniMutex);
#endif
    BeginRead();
    Config c;  // defaults
    c.enabled          = ReadBool(L"Enabled", c.enabled);
    c.customClientId   = util::Trim(ReadStr(L"CustomClientId", L""));
    c.useCustomApp     = ReadBool(L"UseCustomApp", false);
    {   // migrate an own ID from older versions
        std::wstring old = util::Trim(ReadStr(L"ClientId", L""));
        if (c.customClientId.empty() && !old.empty() && old != kDefaultClientId) {
            c.customClientId = old;
            c.useCustomApp = true;
        }
    }
    c.activityType     = ReadInt(L"ActivityType", c.activityType);
    c.statusDisplay    = ReadInt(L"StatusDisplay", c.statusDisplay);
    c.showTimestamps   = ReadBool(L"ShowTimestamps", c.showTimestamps);
    c.pausedBehavior   = ReadInt(L"PausedBehavior", c.pausedBehavior);
    c.clearAfterPaused = ReadInt(L"ClearAfterPausedMin", c.clearAfterPaused);
    c.hideStreams      = ReadBool(L"HideStreams", c.hideStreams);
    c.excludePaths     = ReadStr(L"ExcludePaths", c.excludePaths);

    c.details        = ReadStr(L"Details", c.details);
    c.state          = ReadStr(L"State", c.state);
    const int cfgVersion = ReadInt(L"ConfigVersion", 1);
    if (cfgVersion < 2 && c.state == L"%artist%") c.state = L"by %artist%";  // 1.1 default
    if (cfgVersion < 4 && c.pausedBehavior != 1) c.pausedBehavior = 1;      // 1.3 default: clear while paused (PreMiD wins)
    c.largeText      = ReadStr(L"LargeText", c.largeText);
    c.titleLink       = ReadBool(L"TitleLink", c.titleLink);
    c.titleLinkCustom = ReadBool(L"TitleLinkCustom", c.titleLinkCustom);
    c.titleLinkUrl    = util::Trim(ReadStr(L"TitleLinkUrl", c.titleLinkUrl));
    if (c.titleLinkUrl.empty()) c.titleLinkUrl = kDefaultTitleLink;
    c.smallText      = ReadStr(L"SmallText", c.smallText);
    c.showSmallIcon  = ReadBool(L"ShowSmallIcon", c.showSmallIcon);
    c.barLength      = ReadInt(L"BarLength", c.barLength);
    c.refreshSeconds = ReadInt(L"RefreshSeconds", c.refreshSeconds);

    c.coverEnabled  = ReadBool(L"CoverEnabled", c.coverEnabled);
    c.srcEmbedded   = ReadBool(L"CoverEmbedded", c.srcEmbedded);
    c.srcFolder     = ReadBool(L"CoverFolder", c.srcFolder);
    c.coverNames    = ReadStr(L"CoverNames", c.coverNames);
    c.imgurClientId = util::Trim(ReadStr(L"ImgurClientId", c.imgurClientId));
    {   // migrate 1.0 setting "CoverImgur" if UploadHost was never written
        int def = (ReadBool(L"CoverImgur", false) && !c.imgurClientId.empty()) ? 2 : c.uploadHost;
        c.uploadHost = ReadInt(L"UploadHost", def);
        if (c.uploadHost < 0 || c.uploadHost > 2) c.uploadHost = 1;
    }
    c.preferLocal    = ReadBool(L"CoverPreferLocal", c.preferLocal);
    c.srcSpotify     = ReadBool(L"CoverSpotify", c.srcSpotify);
    c.srcDeezer      = ReadBool(L"CoverDeezer", c.srcDeezer);
    c.srcItunes      = ReadBool(L"CoverItunes", c.srcItunes);
    c.srcBandcamp    = ReadBool(L"CoverBandcamp", c.srcBandcamp);
    c.srcDiscogs     = ReadBool(L"CoverDiscogs", c.srcDiscogs);
    c.srcMusicBrainz = ReadBool(L"CoverMusicBrainz", c.srcMusicBrainz);
    c.spotifyId      = util::Trim(ReadStr(L"SpotifyClientId", c.spotifyId));
    c.spotifySecret  = util::Trim(ReadStr(L"SpotifyClientSecret", c.spotifySecret));
    c.discogsToken   = util::Trim(ReadStr(L"DiscogsToken", c.discogsToken));


    if (c.barLength < 4) c.barLength = 4;
    if (c.barLength > 30) c.barLength = 30;
    if (c.refreshSeconds < 5) c.refreshSeconds = 5;
    if (c.clearAfterPaused < 0) c.clearAfterPaused = 0;
    if (c.activityType != 0 && c.activityType != 2) c.activityType = 2;
    if (c.statusDisplay < 0 || c.statusDisplay > 2) c.statusDisplay = 1;
    if (c.pausedBehavior < 0 || c.pausedBehavior > 1) c.pausedBehavior = 1;

    Resolve(c);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_cfg = c;
    }
#ifndef _WIN32
    // no settings page on Linux: write a complete file with all options once, so it can be edited by hand
    if (ReadInt(L"ConfigVersion", 0) == 0) {
        WriteStuff(c);
        Flush();
    }
#endif
}

bool ReloadIfChanged() {
#ifdef _WIN32
    return false;   // the settings page writes through Set()
#else
    {
        std::lock_guard<std::mutex> ini(g_iniMutex);
        if (FileMtime() == g_iniMtime) return false;
    }
    Load();
    return true;
#endif
}

Config Get() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_cfg;
}

void Set(const Config& in) {
    Config c = in;
    Resolve(c);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_cfg = c;
    }
#ifndef _WIN32
    std::lock_guard<std::mutex> ini(g_iniMutex);
#endif
    WriteStuff(c);
    Flush();
}

}  // namespace config

namespace {

void WriteStuff(const Config& c) {
    WriteBool(L"Enabled", c.enabled);
    WriteBool(L"UseCustomApp", c.useCustomApp);
    WriteStr(L"CustomClientId", c.customClientId);
    WriteBool(L"TitleLink", c.titleLink);
    WriteBool(L"TitleLinkCustom", c.titleLinkCustom);
    WriteStr(L"TitleLinkUrl", c.titleLinkUrl);
    WriteInt(L"ActivityType", c.activityType);
    WriteInt(L"StatusDisplay", c.statusDisplay);
    WriteBool(L"ShowTimestamps", c.showTimestamps);
    WriteInt(L"PausedBehavior", c.pausedBehavior);
    WriteInt(L"ClearAfterPausedMin", c.clearAfterPaused);
    WriteBool(L"HideStreams", c.hideStreams);
    WriteStr(L"ExcludePaths", c.excludePaths);

    WriteStr(L"Details", c.details);
    WriteStr(L"State", c.state);
    WriteInt(L"ConfigVersion", 4);
    WriteStr(L"LargeText", c.largeText);
    WriteStr(L"SmallText", c.smallText);
    WriteBool(L"ShowSmallIcon", c.showSmallIcon);
    WriteInt(L"BarLength", c.barLength);
    WriteInt(L"RefreshSeconds", c.refreshSeconds);

    WriteBool(L"CoverEnabled", c.coverEnabled);
    WriteBool(L"CoverEmbedded", c.srcEmbedded);
    WriteBool(L"CoverFolder", c.srcFolder);
    WriteInt(L"UploadHost", c.uploadHost);
    WriteBool(L"CoverPreferLocal", c.preferLocal);
    WriteBool(L"CoverSpotify", c.srcSpotify);
    WriteBool(L"CoverDeezer", c.srcDeezer);
    WriteBool(L"CoverItunes", c.srcItunes);
    WriteBool(L"CoverBandcamp", c.srcBandcamp);
    WriteBool(L"CoverDiscogs", c.srcDiscogs);
    WriteBool(L"CoverMusicBrainz", c.srcMusicBrainz);
    WriteStr(L"SpotifyClientId", c.spotifyId);
    WriteStr(L"SpotifyClientSecret", c.spotifySecret);
    WriteStr(L"DiscogsToken", c.discogsToken);
    WriteStr(L"CoverNames", c.coverNames);
    WriteStr(L"ImgurClientId", c.imgurClientId);

}

}  // namespace
