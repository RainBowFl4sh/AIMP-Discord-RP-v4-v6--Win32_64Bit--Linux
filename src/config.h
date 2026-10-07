// Persistent plugin settings: DiscordRPC.ini in AIMP's profile folder (usually %APPDATA%\AIMP or ~/.config/AIMP).
// One INI reader / writer for both platforms; the file may be edited by hand while AIMP is running.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

// Built-in defaults shared by every user (never shown in the UI)
inline const wchar_t* const kDefaultClientId  = L"1555109720807702559";   // Discord application "AIMP"
inline const wchar_t* const kDefaultTitleLink = L"https://www.youtube.com/results?search_query=%artist%+%title%";

struct Config {
    // --- General
    bool         enabled          = true;
    int          activityType     = 2;           // 2 = Listening (progress bar), 0 = Playing
    int          statusDisplay    = 1;           // 0 = app name, 1 = state line (artist), 2 = details line (title)
    std::wstring activityName;                   // shown instead of the application name ("Listening to ..."), "" = app name
    bool         showTimestamps   = true;        // progress bar / elapsed time
    int          pausedBehavior   = 1;           // 0 = show "Paused", 1 = clear presence (lets PreMiD etc. show)
    int          clearAfterPaused = 0;           // minutes, 0 = never
    bool         hideStreams      = false;
    std::wstring excludePaths;                   // ';' separated substrings

    // --- Display
    std::wstring details   = L"%title%";
    std::wstring state     = L"by %artist%";
    std::wstring largeText = L"%album%";
    std::wstring smallText = L"%status%";
    bool         showSmallIcon  = true;
    int          barLength      = 12;            // characters for %bar%
    int          refreshSeconds = 15;            // refresh interval if %pos% / %bar% / %percent% are used
    int          rotateSeconds  = 5;             // texts with several variants ("a || b"): switch every N seconds
    // clickable song title (Discord opens the link when someone clicks the title)
    bool         titleLink       = true;
    bool         titleLinkCustom = false;        // override the default YouTube search
    std::wstring titleLinkUrl    = kDefaultTitleLink;

    // --- Cover art
    bool         coverEnabled  = true;
    bool         srcEmbedded   = true;           // tags (ID3v2 / FLAC / MP4)
    bool         srcFolder     = true;           // cover.jpg etc. next to the file
    std::wstring coverNames    = L"cover;folder;front;album;albumart";
    int          uploadHost    = 3;              // local cover upload: 0 = off, 1 = catbox.moe, 2 = Imgur, 3 = x0.at
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

    // --- Advanced
    std::wstring excludePlaylists;               // ';' separated substrings of playlist names
    std::wstring language;                       // "" = AIMP's language, otherwise a language code ("de", "ru", ...)
    bool         useCustomApp   = false;         // use an own Discord application
    std::wstring customClientId;

    // --- Updates (About tab)
    bool         updateCheck     = true;
    int          updateFrequency = 1;            // 0 = every AIMP start, 1 = daily, 2 = weekly, 3 = monthly
    bool         updateAuto      = true;         // download the package and open it in AIMP
    int64_t      updateLastCheck = 0;            // unix time
    std::wstring updateLatest;                   // newest version found by the last check
    std::wstring updateOffered;                  // version already installed automatically once
    std::wstring lastVersion;                    // plugin version of the last start (update finished -> notice)
    std::wstring updateRestarted;                // version AIMP was restarted for by the plugin (popup says why)

    // --- computed, not stored
    std::wstring clientId = kDefaultClientId;    // effective application ID
    std::wstring detailsUrl;                     // effective title link (empty = none)
};

namespace config {
void   SetProfileDir(const std::wstring& dir);   // at start, before Load: AIMP's profile folder (takes over old files)
std::wstring DataDir();          // AIMP's profile folder; without it %APPDATA%\AIMP or $XDG_CONFIG_HOME/AIMP
std::wstring CacheDir();         // DataDir()/DiscordRPC: downloaded images and updates
std::wstring IniPath();
void   Load();                   // read from disk into memory
bool   ReloadIfChanged();        // re-read the file after it was edited by hand (true = reloaded)
Config Get();                    // thread-safe copy
void   Resolve(Config& c);       // fills the computed fields (clientId, detailsUrl)
void   Set(const Config& c);     // update memory + write to disk
void   Update(const std::function<void(Config&)>& change);   // atomic read-modify-write (+ disk)
bool   ExportTo(const std::wstring& path, const Config& c);  // settings only (no update bookkeeping)
bool   ImportFrom(const std::wstring& path);                 // false = not a settings file of this plugin
}  // namespace config
