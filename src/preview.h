// Live preview of the Discord presence on the settings page (Display tab): the activity card of the profile and
// the entry in the member list, drawn like Discord's dark theme (Windows: GDI+, Linux: cairo).
#pragma once
#include "aimp_util.h"

#include <string>

struct PreviewData {
    bool         hidden = false;          // Discord shows nothing - 'message' tells why
    std::wstring message;                 // reason / note under the member list
    std::wstring header;                  // "Listening to AIMP"
    std::wstring details, state, largeText;
    std::wstring memberLine;              // activity text in the member list
    std::wstring userName;
    std::wstring iconTip, linkTip;        // small info lines (tooltips, title link)
    bool         progress = false;        // progress bar (Listening)
    std::wstring elapsed;                 // "00:07 elapsed" (Playing), empty = none
    double       pos = 0, dur = 0;
    bool         playing = true;          // play or pause icon
    bool         smallIcon = true;
    IAIMPImage*  cover = nullptr;         // large image (cover or the application's "aimp" image), may be null
    IAIMPImage*  icon = nullptr;          // play / pause image of the application, null = drawn
    IAIMPImage*  avatar = nullptr;        // the user's Discord avatar, null = drawn
};

const int kPreviewWidth = 455, kPreviewHeight = 136;   // size on the page (96 DPI); drawn in 470 x 140 design units

void DrawPreview(HCANVAS canvas, const RECT& r, const PreviewData& d);
void DrawRoundedImage(HCANVAS canvas, const RECT& r, IAIMPImage* img, float radius);   // corners stay transparent
