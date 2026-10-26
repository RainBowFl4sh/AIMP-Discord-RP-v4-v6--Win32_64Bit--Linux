// Live preview of the Discord presence on the settings page (Display tab): the activity card of the profile and
// the entry in the member list, drawn like Discord's dark theme (Windows: GDI+, Linux: cairo).
#pragma once
#include "aimp_util.h"

#include <string>
#include <vector>

struct PreviewData {
    bool         hidden = false;          // Discord shows nothing - 'message' tells why
    std::wstring message;                 // reason / note under the member list
    std::wstring header;                  // "Listening to AIMP"
    std::wstring details, state, largeText;
    std::wstring memberLine;              // activity text in the member list
    std::wstring userName;
    std::wstring iconTip, linkTip;        // small info lines (tooltips, title link)
    std::wstring buttons[2];              // labels of the buttons under the activity, "" = none
    std::wstring buttonsTip;              // note: only others see the buttons
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

// About tab: "boop!" / "pat" over the author's picture, rising from (ax, ay) (parts of the picture, 0..1) by 'rise'
// (units of a 64 x 64 picture) and fading out; t = 0..1 of the animation.
void DrawAvatarFx(HCANVAS canvas, const RECT& r, const std::wstring& text, float ax, float ay, float rise, float t);

// Placeholder bar above the live preview: group tabs, and the placeholders of the selected group; clicking one
// inserts it at the text cursor (settings_ui.cpp).
// Positions in design units (96 DPI); drawn scaled from designW to the paint box.
struct Chip {
    std::wstring text;
    float x = 0, y = 0, w = 0, h = 0;
    int kind = 0;   // 0 placeholder, 1 group tab, 2 the selected group tab
};
const float kChipFont = 11.5f;
float MeasureChipText(const std::wstring& t);   // width of a button text in design units
void DrawChips(HCANVAS canvas, const RECT& r, float designW, const std::vector<Chip>& chips, int hover);
