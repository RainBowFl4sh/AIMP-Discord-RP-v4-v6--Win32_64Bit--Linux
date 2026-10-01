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

void Load() {
    Config c;  // defaults
    c.enabled          = ReadBool(L"Enabled", c.enabled);
    c.clientId         = util::Trim(ReadStr(L"ClientId", c.clientId));
    c.activityType     = ReadInt(L"ActivityType", c.activityType);
    c.statusDisplay    = ReadInt(L"StatusDisplay", c.statusDisplay);
    c.showTimestamps   = ReadBool(L"ShowTimestamps", c.showTimestamps);
    c.pausedBehavior   = ReadInt(L"PausedBehavior", c.pausedBehavior);
    c.clearAfterPaused = ReadInt(L"ClearAfterPausedMin", c.clearAfterPaused);
    c.hideStreams      = ReadBool(L"HideStreams", c.hideStreams);
    c.excludePaths     = ReadStr(L"ExcludePaths", c.excludePaths);

    c.details        = ReadStr(L"Details", c.details);
    c.state          = ReadStr(L"State", c.state);
    c.largeText      = ReadStr(L"LargeText", c.largeText);
    c.smallText      = ReadStr(L"SmallText", c.smallText);
    c.showSmallIcon  = ReadBool(L"ShowSmallIcon", c.showSmallIcon);
    c.playKey        = ReadStr(L"PlayKey", c.playKey);
    c.pauseKey       = ReadStr(L"PauseKey", c.pauseKey);
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
    c.fallbackKey   = util::Trim(ReadStr(L"FallbackKey", c.fallbackKey));

    c.btn1Enabled = ReadBool(L"Button1Enabled", c.btn1Enabled);
    c.btn1Label   = ReadStr(L"Button1Label", c.btn1Label);
    c.btn1Url     = ReadStr(L"Button1Url", c.btn1Url);
    c.btn2Enabled = ReadBool(L"Button2Enabled", c.btn2Enabled);
    c.btn2Label   = ReadStr(L"Button2Label", c.btn2Label);
    c.btn2Url     = ReadStr(L"Button2Url", c.btn2Url);

    if (c.barLength < 4) c.barLength = 4;
    if (c.barLength > 30) c.barLength = 30;
    if (c.refreshSeconds < 5) c.refreshSeconds = 5;
    if (c.clearAfterPaused < 0) c.clearAfterPaused = 0;
    if (c.activityType != 0 && c.activityType != 2) c.activityType = 2;
    if (c.statusDisplay < 0 || c.statusDisplay > 2) c.statusDisplay = 1;

    std::lock_guard<std::mutex> lk(g_mutex);
    g_cfg = c;
}

Config Get() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_cfg;
}

void Set(const Config& c) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_cfg = c;
    }
    WriteBool(L"Enabled", c.enabled);
    WriteStr(L"ClientId", c.clientId);
    WriteInt(L"ActivityType", c.activityType);
    WriteInt(L"StatusDisplay", c.statusDisplay);
    WriteBool(L"ShowTimestamps", c.showTimestamps);
    WriteInt(L"PausedBehavior", c.pausedBehavior);
    WriteInt(L"ClearAfterPausedMin", c.clearAfterPaused);
    WriteBool(L"HideStreams", c.hideStreams);
    WriteStr(L"ExcludePaths", c.excludePaths);

    WriteStr(L"Details", c.details);
    WriteStr(L"State", c.state);
    WriteStr(L"LargeText", c.largeText);
    WriteStr(L"SmallText", c.smallText);
    WriteBool(L"ShowSmallIcon", c.showSmallIcon);
    WriteStr(L"PlayKey", c.playKey);
    WriteStr(L"PauseKey", c.pauseKey);
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
    WriteStr(L"FallbackKey", c.fallbackKey);

    WriteBool(L"Button1Enabled", c.btn1Enabled);
    WriteStr(L"Button1Label", c.btn1Label);
    WriteStr(L"Button1Url", c.btn1Url);
    WriteBool(L"Button2Enabled", c.btn2Enabled);
    WriteStr(L"Button2Label", c.btn2Label);
    WriteStr(L"Button2Url", c.btn2Url);
}

}  // namespace config
