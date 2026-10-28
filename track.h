// Plain-data description of the currently playing track (no AIMP types, safe to pass between threads).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct TrackInfo {
    std::wstring artist;
    std::wstring albumArtist;
    std::wstring title;
    std::wstring album;
    std::wstring genre;
    std::wstring year;
    std::wstring trackNumber;
    std::wstring fileName;   // full path or URL
    std::wstring playlist;   // name of the playlist the track was started from (may be empty)
    std::wstring next;       // the track AIMP plays next ("Artist - Title"), only read when %next% is used
    // advanced placeholders only (otherwise empty): lower-case name -> value, e.g. "comment", "playcount"
    std::map<std::wstring, std::wstring> extra;
    // the cover in the file's tags as AIMP read it - only for formats the plugin cannot read itself
    // (Ogg / Opus, WMA, APE, WavPack, ...), only when local covers are uploaded; null = none
    std::shared_ptr<const std::vector<uint8_t>> art;
};
