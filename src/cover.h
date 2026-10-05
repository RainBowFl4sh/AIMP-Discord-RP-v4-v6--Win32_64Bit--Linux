// Finds a publicly reachable cover URL for the current track:
//   local cover (tags / folder image) -> upload to catbox.moe (no key) or Imgur
//   online lookup: Spotify, Deezer, iTunes, Bandcamp, Discogs, MusicBrainz/Cover Art Archive
//   -> (caller falls back to a Discord asset key)
// Results are cached on disk, so every cover is uploaded / looked up only once.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "config.h"
#include "track.h"

struct CoverResult {
    std::string url;
    std::string source;   // "Deezer", "iTunes", ... or "embedded|catbox.moe" / "folder|x0.at" for uploaded local covers
    int64_t     stored = 0;   // unix time it was cached (uploaded covers are checked again when they are older)
};

class CoverResolver {
public:
    CoverResolver();

    CoverResult PeekCache(const TrackInfo& t);                       // fast, never touches the network
    CoverResult Resolve(const TrackInfo& t, const Config& cfg);      // may block (network) - call from worker thread
    void        ClearCache();
    void        Forget(const TrackInfo& t);                          // drop this track's cover (resolved again)
    static bool Reachable(const std::string& url);                   // the URL delivers a picture (first bytes)

    static bool EnsureImaging();                                     // Windows: GDI+ started (also for the preview)
    static void ShutdownImaging();                                   // GDI+ shutdown, call once on unload

private:
    void LoadCache();
    void StoreCache(const std::string& key, const CoverResult& r);

    std::mutex                          mu_;
    std::wstring                        path_;
    std::map<std::string, CoverResult>  cache_;
    std::map<std::string, uint64_t>     negative_;   // failed lookups -> retry after 10 minutes
    std::map<std::string, bool>         checked_;    // uploaded covers checked again in this session
};
