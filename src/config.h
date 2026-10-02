// Persistent plugin settings (Windows: %APPDATA%\AIMP\DiscordRPC.ini, Linux: ~/.config/AIMP/DiscordRPC.ini).
#pragma once
#include <string>

// Built-in defaults shared by every user (never shown in the UI)
inline const wchar_t* const kDefaultClientId  = L"1555109720807702559";   // Discord application "AIMP"
inline const wchar_t* const kDefaultTitleLink = L"https://www.youtube.com/results?search_query=%artist%+%title%";

struct Config {
    // --- General ---
    bool         enabled          = true;
    bool         useCustomApp     = false;       // advanced: use an own Discord application
    std::wstring customClientId;
    std::wstring clientId = kDefaultClientId;    // effective ID (computed, not stored)
    int          activityType     = 2;           // 2 = Listening (progress bar), 0 = Playing
    int          statusDisplay    = 1;           // 0 = app name, 1 = state line (artist), 2 = details line (title)
    bool         showTimestamps   = true;        // progress bar / elapsed time
    int          pausedBehavior   = 1;           // 0 = show "Paused", 1 = clear presence (lets PreMiD etc. show)
    int          clearAfterPaused = 0;           // minutes, 0 = never
    bool         hideStreams      = false;
    std::wstring excludePaths;                   // ';' separated substrings

    // --- Text ---
    std::wstring details   = L"%title%";
    std::wstring state     = L"by %artist%";
    // clickable song title (Discord opens the link when someone clicks the title)
    bool         titleLink       = true;
    bool         titleLinkCustom = false;        // override the default YouTube search
    std::wstring titleLinkUrl    = kDefaultTitleLink;
    std::wstring detailsUrl, stateUrl;           // effective links (computed, not stored)
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
std::wstring DataDir();          // %APPDATA%\AIMP  or  $XDG_CONFIG_HOME/AIMP
void   Load();                   // read from disk into memory
bool   ReloadIfChanged();        // Linux: re-read the file after it was edited by hand (true = reloaded)
Config Get();                    // thread-safe copy
void   Set(const Config& c);     // update memory + write to disk
}  // namespace config
