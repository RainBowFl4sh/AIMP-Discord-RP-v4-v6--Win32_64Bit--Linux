// Plain-data description of the currently playing track (no AIMP types, safe to pass between threads).
#pragma once
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
};
