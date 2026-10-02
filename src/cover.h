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

class CoverResolver {
public:
    CoverResolver();

    std::string PeekCache(const TrackInfo& t);                       // fast, never touches the network
    std::string Resolve(const TrackInfo& t, const Config& cfg);      // may block (network) - call from worker thread
    void        ClearCache();

    static void ShutdownImaging();                                   // GDI+ shutdown, call once on unload

private:
    void LoadCache();
    void StoreCache(const std::string& key, const std::string& url);

    std::mutex                          mu_;
    std::wstring                        path_;
    std::map<std::string, std::string>  cache_;
    std::map<std::string, uint64_t>     negative_;   // failed lookups -> retry after 10 minutes
};
