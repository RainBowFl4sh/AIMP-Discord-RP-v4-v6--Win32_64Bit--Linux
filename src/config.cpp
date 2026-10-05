#include "config.h"

#include <cwchar>
#include <map>
#include <mutex>

#include "util.h"
#include "version.h"

namespace {

std::mutex g_mutex;     // g_cfg
Config     g_cfg;
std::mutex g_fileMu;    // file access + g_stamp; serializes writers
uint64_t   g_stamp = 0;

const wchar_t* const kSection = L"DiscordRPC";
const int kConfigVersion = 6;   // 5 = 1.5.0 (one INI format for both platforms, UTF-16 on Windows), 6 = 1.5.3 (x0.at)

#ifdef _WIN32
const wchar_t* const kEol = L"\r\n";
#else
const wchar_t* const kEol = L"\n";
#endif

// Reads values from the parsed [DiscordRPC] section; missing / invalid entries keep the default.
struct Reader {
    std::map<std::wstring, std::wstring> m;   // lower-case keys
    void Section(const wchar_t*) {}
    const std::wstring* Find(const wchar_t* k) const {
        auto it = m.find(util::Lower(k));
        return it == m.end() ? nullptr : &it->second;
    }
    void Str(const wchar_t* k, std::wstring& v) const {
        if (const std::wstring* p = Find(k)) v = *p;
    }
    void Num(const wchar_t* k, long long& v) const {
        const std::wstring* p = Find(k);
        if (!p) return;
        wchar_t* end = nullptr;
        long long x = wcstoll(p->c_str(), &end, 10);
        if (end != p->c_str()) v = x;
    }
    void Int(const wchar_t* k, int& v) const { long long x = v; Num(k, x); v = (int)x; }
    void I64(const wchar_t* k, int64_t& v) const { long long x = v; Num(k, x); v = (int64_t)x; }
    void Bool(const wchar_t* k, bool& v) const { long long x = v ? 1 : 0; Num(k, x); v = x != 0; }
};

// Writes "key=value" lines (one line per value) in the order of the Visit functions.
struct Writer {
    std::wstring t;
    void Section(const wchar_t* comment) { t += kEol; t += L"; "; t += comment; t += kEol; }
    void Str(const wchar_t* k, const std::wstring& v) {
        t += k;
        t += L'=';
        t += util::ReplaceAll(util::ReplaceAll(v, L"\r", L" "), L"\n", L" ");
        t += kEol;
    }
    void Int(const wchar_t* k, long long v) { Str(k, std::to_wstring(v)); }
    void I64(const wchar_t* k, int64_t v) { Int(k, (long long)v); }
    void Bool(const wchar_t* k, bool v) { Int(k, v ? 1 : 0); }
};

// The one list of all settings (used for reading, writing and exporting).
template <class C, class V>
void VisitSettings(C& c, V& v) {
    v.Section(L"General");
    v.Bool(L"Enabled", c.enabled);
    v.Int(L"ActivityType", c.activityType);
    v.Int(L"StatusDisplay", c.statusDisplay);
    v.Bool(L"ShowTimestamps", c.showTimestamps);
    v.Int(L"PausedBehavior", c.pausedBehavior);
    v.Int(L"ClearAfterPausedMin", c.clearAfterPaused);
    v.Bool(L"HideStreams", c.hideStreams);
    v.Str(L"ExcludePaths", c.excludePaths);
    v.Section(L"Display (placeholders: %artist% %title% %album% %albumartist% %genre% %year% %track% %playlist% "
              L"%filename% %ext% %pos% %dur% %percent% %bar% %status%)");
    v.Str(L"Details", c.details);
    v.Str(L"State", c.state);
    v.Str(L"LargeText", c.largeText);
    v.Str(L"SmallText", c.smallText);
    v.Bool(L"ShowSmallIcon", c.showSmallIcon);
    v.Int(L"BarLength", c.barLength);
    v.Int(L"RefreshSeconds", c.refreshSeconds);
    v.Bool(L"TitleLink", c.titleLink);
    v.Bool(L"TitleLinkCustom", c.titleLinkCustom);
    v.Str(L"TitleLinkUrl", c.titleLinkUrl);
    v.Section(L"Cover art (UploadHost: 0 = off, 1 = catbox.moe, 2 = Imgur, 3 = x0.at)");
    v.Bool(L"CoverEnabled", c.coverEnabled);
    v.Bool(L"CoverEmbedded", c.srcEmbedded);
    v.Bool(L"CoverFolder", c.srcFolder);
    v.Str(L"CoverNames", c.coverNames);
    v.Int(L"UploadHost", c.uploadHost);
    v.Str(L"ImgurClientId", c.imgurClientId);
    v.Bool(L"CoverPreferLocal", c.preferLocal);
    v.Bool(L"CoverSpotify", c.srcSpotify);
    v.Str(L"SpotifyClientId", c.spotifyId);
    v.Str(L"SpotifyClientSecret", c.spotifySecret);
    v.Bool(L"CoverDeezer", c.srcDeezer);
    v.Bool(L"CoverItunes", c.srcItunes);
    v.Bool(L"CoverBandcamp", c.srcBandcamp);
    v.Bool(L"CoverDiscogs", c.srcDiscogs);
    v.Str(L"DiscogsToken", c.discogsToken);
    v.Bool(L"CoverMusicBrainz", c.srcMusicBrainz);
    v.Section(L"Advanced (Language: empty = AIMP's language, or en / de / ru / uk)");
    v.Str(L"ExcludePlaylists", c.excludePlaylists);
    v.Str(L"Language", c.language);
    v.Bool(L"UseCustomApp", c.useCustomApp);
    v.Str(L"CustomClientId", c.customClientId);
    v.Section(L"Updates (UpdateFrequency: 0 = every AIMP start, 1 = daily, 2 = weekly, 3 = monthly)");
    v.Bool(L"UpdateCheck", c.updateCheck);
    v.Int(L"UpdateFrequency", c.updateFrequency);
    v.Bool(L"UpdateAuto", c.updateAuto);
}

template <class C, class V>
void VisitState(C& c, V& v) {   // bookkeeping of the update check, not exported
    v.I64(L"UpdateLastCheck", c.updateLastCheck);
    v.Str(L"UpdateLatest", c.updateLatest);
    v.Str(L"UpdateOffered", c.updateOffered);
    v.Str(L"LastVersion", c.lastVersion);
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

Config FromText(const std::wstring& text, bool* found = nullptr, long long* version = nullptr) {
    Reader r;
    for (auto& kv : util::IniSection(text, kSection, found)) r.m[util::Lower(kv.first)] = kv.second;
    Config c;   // defaults
    VisitSettings(c, r);
    VisitState(c, r);
    long long ver = 1;
    r.Num(L"ConfigVersion", ver);
    if (version) *version = r.Find(L"ConfigVersion") ? ver : 0;

    // migrations from older versions
    if (!r.Find(L"UseCustomApp")) {   // 1.1: own ID in "ClientId"
        std::wstring old;
        r.Str(L"ClientId", old);
        old = util::Trim(old);
        if (!old.empty() && old != kDefaultClientId) {
            c.customClientId = old;
            c.useCustomApp = true;
        }
    }
    if (ver < 2 && c.state == L"%artist%") c.state = L"by %artist%";   // 1.1 default
    if (ver < 4) c.pausedBehavior = 1;                                // 1.3: hide while paused (PreMiD wins)
    if (ver < 6 && c.uploadHost == 1) c.uploadHost = 3;               // 1.5.3: catbox.moe (old default) -> x0.at
    if (!r.Find(L"UploadHost")) {                                    // 1.0: "CoverImgur"
        bool imgur = false;
        r.Bool(L"CoverImgur", imgur);
        if (imgur && !util::Trim(c.imgurClientId).empty()) c.uploadHost = 2;
    }

    for (std::wstring* s : {&c.customClientId, &c.titleLinkUrl, &c.imgurClientId, &c.spotifyId, &c.spotifySecret,
                            &c.discogsToken, &c.language})
        *s = util::Trim(*s);
    if (c.titleLinkUrl.empty()) c.titleLinkUrl = kDefaultTitleLink;
    c.barLength = Clamp(c.barLength, 4, 30);
    if (c.refreshSeconds < 5) c.refreshSeconds = 5;
    if (c.clearAfterPaused < 0) c.clearAfterPaused = 0;
    if (c.activityType != 0 && c.activityType != 2) c.activityType = 2;
    if (c.statusDisplay < 0 || c.statusDisplay > 2) c.statusDisplay = 1;
    c.pausedBehavior = Clamp(c.pausedBehavior, 0, 1);
    if (c.uploadHost < 0 || c.uploadHost > 3) c.uploadHost = 3;
    c.updateFrequency = Clamp(c.updateFrequency, 0, 3);
    return c;
}

void Store(Config c) {   // publishes the config (with its computed fields)
    config::Resolve(c);
    std::lock_guard<std::mutex> lk(g_mutex);
    g_cfg = std::move(c);
}

std::string Utf8Bom(const std::wstring& text) { return "\xEF\xBB\xBF" + util::ToUtf8(text); }

void WriteLocked(Config c) {   // g_fileMu held
    Writer w;
    w.t = L"[DiscordRPC]";
    w.t += kEol;
    w.Int(L"ConfigVersion", kConfigVersion);
    VisitSettings(c, w);
    w.Section(L"Update check state");
    VisitState(c, w);
#ifdef _WIN32
    // UTF-16LE with BOM: any text works, and the Windows profile API (older plugin versions) reads it too
    std::string bytes("\xFF\xFE", 2);
    for (wchar_t ch : w.t) {
        bytes += (char)(ch & 0xFF);
        bytes += (char)((ch >> 8) & 0xFF);
    }
#else
    std::string bytes = util::ToUtf8(w.t);
#endif
    if (!util::WriteFileBytes(config::IniPath(), bytes)) util::Log(L"Could not write %ls", config::IniPath().c_str());
    g_stamp = util::FileStamp(config::IniPath());
}

// AIMP's profile folder (set at start); the plugin keeps its files there, as the AIMP plugin rules ask
std::wstring g_profileDir;

// where versions before 1.5.3 kept their files (also used when AIMP does not tell its profile folder)
std::wstring LegacyDir() {
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

std::wstring Normalized(std::wstring dir) {
    while (dir.size() > 1 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
#ifdef _WIN32
    dir = util::Lower(dir);
#endif
    return dir;
}

}  // namespace

namespace config {

void SetProfileDir(const std::wstring& profile) {
    std::wstring dir = profile;
    while (dir.size() > 1 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    if (dir.empty()) return;
    util::MakeDir(dir);
    const std::wstring legacy = LegacyDir();
    g_profileDir = dir;
    if (Normalized(dir) == Normalized(legacy)) return;
    // portable AIMP and other setups: take over the settings of the old location once
    const std::wstring sep(1, util::kPathSep);
    if (util::IsRegularFile(dir + sep + L"DiscordRPC.ini") || !util::IsRegularFile(legacy + sep + L"DiscordRPC.ini"))
        return;
    for (const wchar_t* name : {L"DiscordRPC.ini", L"DiscordRPC_covers.tsv"}) {
        std::string bytes;
        if (util::ReadFileBytes(legacy + sep + name, bytes, 8u << 20)) util::WriteFileBytes(dir + sep + name, bytes);
    }
    util::Log(L"Settings taken over from %ls into AIMP's profile folder %ls", legacy.c_str(), dir.c_str());
}

std::wstring DataDir() {
    if (!g_profileDir.empty()) return g_profileDir;
    return LegacyDir();
}

std::wstring CacheDir() {
    std::wstring dir = DataDir() + util::kPathSep + L"DiscordRPC";
    util::MakeDir(dir);
    return dir;
}

std::wstring IniPath() { return DataDir() + util::kPathSep + L"DiscordRPC.ini"; }

void Load() {
    std::lock_guard<std::mutex> f(g_fileMu);
    std::string raw;
    util::ReadFileBytes(IniPath(), raw, 1u << 20);
    const std::wstring text = util::DecodeText(raw);
    g_stamp = util::FileStamp(IniPath());
    long long ver = 0;
    Config c = FromText(text, nullptr, &ver);
    Store(c);
    // first start / older version: write a complete file with all options (also converts the encoding)
    if (ver < kConfigVersion) WriteLocked(c);
}

bool ReloadIfChanged() {
    {
        std::lock_guard<std::mutex> f(g_fileMu);
        if (util::FileStamp(IniPath()) == g_stamp) return false;
    }
    Load();
    return true;
}

Config Get() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_cfg;
}

void Resolve(Config& c) {
    c.clientId = (c.useCustomApp && !c.customClientId.empty()) ? c.customClientId : std::wstring(kDefaultClientId);
    if (!c.titleLink)           c.detailsUrl.clear();
    else if (c.titleLinkCustom) c.detailsUrl = c.titleLinkUrl.empty() ? std::wstring(kDefaultTitleLink) : c.titleLinkUrl;
    else                        c.detailsUrl = kDefaultTitleLink;
}

void Update(const std::function<void(Config&)>& change) {
    std::lock_guard<std::mutex> f(g_fileMu);
    Config c = Get();
    change(c);
    Store(c);
    WriteLocked(c);
}

void Set(const Config& in) {
    Update([&](Config& c) { c = in; });
}

bool ExportTo(const std::wstring& path, const Config& in) {
    Config c = in;
    Writer w;
    w.t = L"; AIMP Discord Rich Presence " AIMP_DISCORD_RPC_VERSION_W L" - settings (Advanced -> Import)";
    w.t += kEol;
    w.t += L"[DiscordRPC]";
    w.t += kEol;
    w.Int(L"ConfigVersion", kConfigVersion);
    VisitSettings(c, w);
    return util::WriteFileBytes(path, Utf8Bom(w.t));
}

bool ImportFrom(const std::wstring& path) {
    std::string raw;
    if (!util::ReadFileBytes(path, raw, 1u << 20)) return false;
    bool found = false;
    Config imported = FromText(util::DecodeText(raw), &found);
    if (!found) return false;
    Update([&](Config& c) {
        imported.updateLastCheck = c.updateLastCheck;   // keep the update bookkeeping of this PC
        imported.updateLatest = c.updateLatest;
        imported.updateOffered = c.updateOffered;
        imported.lastVersion = c.lastVersion;
        c = imported;
    });
    return true;
}

}  // namespace config
