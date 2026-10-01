#include "config.h"

#include <windows.h>

#include <mutex>

#include "util.h"

namespace {

std::mutex g_mutex;
Config     g_cfg;

const wchar_t* kSection = L"DiscordRPC";

std::wstring IniPath() { return config::DataDir() + L"\\DiscordRPC.ini"; }

std::wstring ReadStr(const wchar_t* key, const std::wstring& def) {
    std::wstring buf(4096, L'\0');
    DWORD n = GetPrivateProfileStringW(kSection, key, def.c_str(), &buf[0], (DWORD)buf.size(), IniPath().c_str());
    buf.resize(n);
    return buf;
}
int ReadInt(const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(kSection, key, def, IniPath().c_str());
}
bool ReadBool(const wchar_t* key, bool def) { return ReadInt(key, def ? 1 : 0) != 0; }

void WriteStr(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(kSection, key, v.c_str(), IniPath().c_str());
}
void WriteInt(const wchar_t* key, int v) { WriteStr(key, std::to_wstring(v)); }
void WriteBool(const wchar_t* key, bool v) { WriteInt(key, v ? 1 : 0); }

}  // namespace

namespace config {

std::wstring DataDir() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) + L"\\AIMP" : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
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
    std::lock_guard<std::mutex> lk(g_mutex);
    g_cfg = c;
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

}  // namespace config
