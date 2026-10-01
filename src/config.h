// Persistent plugin settings (stored in %APPDATA%\AIMP\DiscordRPC.ini).
#pragma once
#include <string>

struct Config {
    // --- General ---
    bool         enabled          = true;
    std::wstring clientId = L"1555109720807702559";  // Discord application ID ("AIMP")
    int          activityType     = 2;           // 2 = Listening (progress bar), 0 = Playing
    int          statusDisplay    = 1;           // 0 = app name, 1 = state line (artist), 2 = details line (title)
    bool         showTimestamps   = true;        // progress bar / elapsed time
    int          pausedBehavior   = 0;           // 0 = show "Paused", 1 = clear presence
    int          clearAfterPaused = 0;           // minutes, 0 = never
    bool         hideStreams      = false;
    std::wstring excludePaths;                   // ';' separated substrings

    // --- Text ---
    std::wstring details   = L"%title%";
    std::wstring state     = L"%artist%";
    std::wstring largeText = L"%album%";
    std::wstring smallText = L"%status%";
    bool         showSmallIcon = true;
    std::wstring playKey   = L"play";
    std::wstring pauseKey  = L"pause";
    int          barLength = 12;                 // characters for %bar%
    int          refreshSeconds = 15;            // refresh interval if %pos% / %bar% / %percent% are used

    // --- Cover art ---
    bool         coverEnabled  = true;
    bool         srcEmbedded   = true;           // tags (ID3v2 / FLAC / MP4)
    bool         srcFolder     = true;           // cover.jpg etc. next to the file
    std::wstring coverNames    = L"cover;folder;front;album;albumart";
    int          uploadHost    = 1;              // local cover upload: 0 = off, 1 = catbox.moe (no key), 2 = Imgur
    bool         preferLocal   = true;           // local cover first, online lookup only if none found
    std::wstring imgurClientId;
    // online lookup (public URLs, nothing is uploaded)
    bool         srcSpotify     = false;         // needs Client ID + Secret
    bool         srcDeezer      = true;
    bool         srcItunes      = true;
    bool         srcBandcamp    = true;
    bool         srcDiscogs     = false;         // needs personal access token
    bool         srcMusicBrainz = true;          // MusicBrainz + Cover Art Archive
    std::wstring spotifyId, spotifySecret, discogsToken;
    std::wstring fallbackKey   = L"aimp";        // asset key uploaded in the Developer Portal

    // --- Buttons (max. 2 in Discord) ---
    bool         btn1Enabled = false;
    std::wstring btn1Label   = L"Search on YouTube";
    std::wstring btn1Url     = L"https://www.youtube.com/results?search_query=%artist%+%title%";
    bool         btn2Enabled = false;
    std::wstring btn2Label   = L"Search on Last.fm";
    std::wstring btn2Url     = L"https://www.last.fm/search?q=%artist%+%title%";
};

namespace config {
std::wstring DataDir();          // %APPDATA%\AIMP
void   Load();                   // read from disk into memory
Config Get();                    // thread-safe copy
void   Set(const Config& c);     // update memory + write to disk
}  // namespace config
