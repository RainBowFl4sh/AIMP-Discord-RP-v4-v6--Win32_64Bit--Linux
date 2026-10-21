// Plain-data description of the currently playing track (no AIMP types, safe to pass between threads).
#pragma once
#include <map>
#include <string>

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
};
