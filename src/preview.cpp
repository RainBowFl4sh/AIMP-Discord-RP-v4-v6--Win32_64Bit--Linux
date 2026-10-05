#include "preview.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _WIN32
#include <objidl.h>
using std::max;   // gdiplus.h expects these when NOMINMAX is defined
using std::min;
#include <gdiplus.h>
#include "cover.h"   // CoverResolver::EnsureImaging (GDI+ start-up)
#endif

#include "util.h"

namespace {

// Discord's dark theme
const uint32_t kPanel = 0x121214, kCard = 0x1f1f23, kRow = 0x232328, kWhite = 0xf2f3f5, kText = 0xdbdee1,
               kMuted = 0x949ba4, kTrack = 0x4e5058, kGreen = 0x23a55a, kBlurple = 0x5865f2, kOrange = 0xf5a623;

const float kPi = 3.14159265f;
const float kDesignW = 470, kDesignH = 140;   // design units; scaled to the paint box (kPreviewWidth x kPreviewHeight)

// ---------------------------------------------------------------- platform painter (design units, 96 DPI)

#ifdef _WIN32

using namespace Gdiplus;

Color C(uint32_t rgb) { return Color(255, (BYTE)(rgb >> 16), (BYTE)(rgb >> 8), (BYTE)rgb); }

void AddRound(GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

class Painter {
public:
    Painter(HCANVAS dc, const RECT& r) : g_(dc) {
        s_ = (r.right - r.left) / kDesignW;
        ox_ = (float)r.left;
        oy_ = (float)r.top;
        g_.SetSmoothingMode(SmoothingModeAntiAlias);
        g_.SetPixelOffsetMode(PixelOffsetModeHalf);
        g_.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        ok_ = g_.GetLastStatus() == Gdiplus::Ok;
    }
    bool Valid() const { return ok_; }

    void Round(float x, float y, float w, float h, float r, uint32_t rgb) {
        GraphicsPath p;
        AddRound(p, X(x), Y(y), w * s_, h * s_, r * s_);
        SolidBrush b(C(rgb));
        g_.FillPath(&b, &p);
    }
    void Circle(float cx, float cy, float r, uint32_t rgb) {
        SolidBrush b(C(rgb));
        g_.FillEllipse(&b, X(cx - r), Y(cy - r), 2 * r * s_, 2 * r * s_);
    }
    void Triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t rgb) {
        PointF pt[3] = {PointF(X(x1), Y(y1)), PointF(X(x2), Y(y2)), PointF(X(x3), Y(y3))};
        SolidBrush b(C(rgb));
        g_.FillPolygon(&b, pt, 3);
    }
    float Measure(const std::wstring& t, float size, bool bold) {
        Font f(Family(bold), size * s_, Style(bold), UnitPixel);
        RectF box;
        g_.MeasureString(t.c_str(), (INT)t.size(), &f, PointF(0, 0), StringFormat::GenericTypographic(), &box);
        return box.Width / s_;
    }
    void Draw(const std::wstring& t, float x, float y, float size, bool bold, uint32_t rgb) {
        Font f(Family(bold), size * s_, Style(bold), UnitPixel);
        SolidBrush b(C(rgb));
        g_.DrawString(t.c_str(), (INT)t.size(), &f, PointF(X(x), Y(y)), StringFormat::GenericTypographic(), &b);
    }
    // AIMP draws the image; rounded corners / circle: the corners are painted over with the background colour
    void Image(IAIMPImage* img, float x, float y, float w, float h, float r, uint32_t bg) {
        RECT rc = {(LONG)lroundf(X(x)), (LONG)lroundf(Y(y)), (LONG)lroundf(X(x + w)), (LONG)lroundf(Y(y + h))};
        HDC dc = g_.GetHDC();
        img->Draw(dc, rc, AIMP_IMAGE_DRAW_STRETCHMODE_FILL | AIMP_IMAGE_DRAW_QUALITY_HIGH, nullptr);
        g_.ReleaseHDC(dc);
        GraphicsPath p(FillModeAlternate);
        p.AddRectangle(RectF(X(x) - 1, Y(y) - 1, w * s_ + 2, h * s_ + 2));
        if (r < 0) p.AddEllipse(X(x), Y(y), w * s_, h * s_);
        else AddRound(p, X(x), Y(y), w * s_, h * s_, r * s_);
        SolidBrush b(C(bg));
        g_.FillPath(&b, &p);
    }

private:
    float X(float x) const { return ox_ + x * s_; }
    float Y(float y) const { return oy_ + y * s_; }
    // Discord uses a semi-bold font; "Segoe UI Semibold" is a separate family on Windows
    const FontFamily* Family(bool bold) {
        if (!fams_) {
            fams_ = true;
            regular_ = new FontFamily(L"Segoe UI");
            if (!regular_->IsAvailable()) { delete regular_; regular_ = FontFamily::GenericSansSerif()->Clone(); }
            semi_ = new FontFamily(L"Segoe UI Semibold");
            semiOk_ = semi_->IsAvailable() != FALSE;
        }
        return bold && semiOk_ ? semi_ : regular_;
    }
    INT Style(bool bold) const { return bold && !semiOk_ ? FontStyleBold : FontStyleRegular; }

public:
    ~Painter() {
        delete regular_;
        delete semi_;
    }

private:
    Graphics g_;
    float s_ = 1, ox_ = 0, oy_ = 0;
    bool ok_ = false, fams_ = false, semiOk_ = false;
    FontFamily* regular_ = nullptr;
    FontFamily* semi_ = nullptr;
};

#else  // Linux: cairo (the canvas AIMP passes is a cairo_t*)

class Painter {
public:
    Painter(HCANVAS cr, const RECT& r) : cr_(cr) {
        s_ = (r.right - r.left) / kDesignW;
        cairo_save(cr_);
        cairo_translate(cr_, r.left, r.top);
        cairo_scale(cr_, s_, s_);
    }
    ~Painter() { cairo_restore(cr_); }
    bool Valid() const { return cr_ != nullptr; }

    void Round(float x, float y, float w, float h, float r, uint32_t rgb) {
        Path(x, y, w, h, r);
        Fill(rgb);
    }
    void Circle(float cx, float cy, float r, uint32_t rgb) {
        cairo_new_path(cr_);
        cairo_arc(cr_, cx, cy, r, 0, 2 * kPi);
        Fill(rgb);
    }
    void Triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t rgb) {
        cairo_new_path(cr_);
        cairo_move_to(cr_, x1, y1);
        cairo_line_to(cr_, x2, y2);
        cairo_line_to(cr_, x3, y3);
        cairo_close_path(cr_);
        Fill(rgb);
    }
    float Measure(const std::wstring& t, float size, bool bold) {
        Font(size, bold);
        cairo_text_extents_t e;
        cairo_text_extents(cr_, util::ToUtf8(t).c_str(), &e);
        return (float)e.x_advance;
    }
    void Draw(const std::wstring& t, float x, float y, float size, bool bold, uint32_t rgb) {
        Font(size, bold);
        cairo_font_extents_t fe;
        cairo_font_extents(cr_, &fe);
        cairo_move_to(cr_, x, y + fe.ascent);
        Source(rgb);
        cairo_show_text(cr_, util::ToUtf8(t).c_str());
        cairo_new_path(cr_);
    }
    void Image(IAIMPImage* img, float x, float y, float w, float h, float r, uint32_t) {
        cairo_save(cr_);
        if (r < 0) {
            cairo_new_path(cr_);
            cairo_arc(cr_, x + w / 2, y + h / 2, w / 2, 0, 2 * kPi);
        } else {
            Path(x, y, w, h, r);
        }
        cairo_clip(cr_);
        // AIMP draws in device units: undo our scaling for the call
        double dx = x, dy = y, dx2 = x + w, dy2 = y + h;
        cairo_user_to_device(cr_, &dx, &dy);
        cairo_user_to_device(cr_, &dx2, &dy2);
        cairo_identity_matrix(cr_);
        RECT rc = {(INT32)lround(dx), (INT32)lround(dy), (INT32)lround(dx2), (INT32)lround(dy2)};
        img->Draw(cr_, rc, AIMP_IMAGE_DRAW_STRETCHMODE_FILL | AIMP_IMAGE_DRAW_QUALITY_HIGH, nullptr);
        cairo_restore(cr_);
    }

private:
    void Path(float x, float y, float w, float h, float r) {
        cairo_new_path(cr_);
        cairo_new_sub_path(cr_);
        cairo_arc(cr_, x + w - r, y + r, r, -kPi / 2, 0);
        cairo_arc(cr_, x + w - r, y + h - r, r, 0, kPi / 2);
        cairo_arc(cr_, x + r, y + h - r, r, kPi / 2, kPi);
        cairo_arc(cr_, x + r, y + r, r, kPi, 3 * kPi / 2);
        cairo_close_path(cr_);
    }
    void Source(uint32_t rgb) {
        cairo_set_source_rgb(cr_, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
    }
    void Fill(uint32_t rgb) {
        Source(rgb);
        cairo_fill(cr_);
    }
    void Font(float size, bool bold) {
        cairo_select_font_face(cr_, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                               bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr_, size);
    }

    cairo_t* cr_;
    float s_ = 1;
};

#endif

// ---------------------------------------------------------------- shared layout

// one line, cut with "…" to fit 'w'; right = right aligned
void Text(Painter& p, std::wstring t, float x, float y, float w, float size, bool bold, uint32_t rgb,
          bool right = false) {
    if (t.empty() || w <= 0) return;
    float tw = p.Measure(t, size, bold);
    if (tw > w) {
        size_t lo = 0, hi = t.size();   // longest prefix that fits together with the ellipsis
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (p.Measure(t.substr(0, mid) + L"…", size, bold) <= w) lo = mid;
            else hi = mid - 1;
        }
        t = util::Trim(t.substr(0, lo)) + L"…";
        tw = p.Measure(t, size, bold);
    }
    p.Draw(t, right ? x + w - tw : x, y, size, bold, rgb);
}

// word-wrapped text, at most maxLines lines
void Wrapped(Painter& p, const std::wstring& t, float x, float y, float w, float size, uint32_t rgb, int maxLines,
             float lineH) {
    std::vector<std::wstring> lines;
    std::wstring line;
    for (const std::wstring& word : util::Split(t, L' ')) {
        std::wstring next = line.empty() ? word : line + L" " + word;
        if (!line.empty() && p.Measure(next, size, false) > w) {
            lines.push_back(line);
            line = word;
        } else {
            line = next;
        }
    }
    if (!line.empty()) lines.push_back(line);
    for (int i = 0; i < (int)lines.size() && i < maxLines; ++i) {
        std::wstring l = lines[i];
        if (i == maxLines - 1 && (int)lines.size() > maxLines) l += L" …";
        Text(p, l, x, y + i * lineH, w, size, false, rgb);
    }
}

void PlayIcon(Painter& p, float cx, float cy, float r, bool playing) {   // like the "play" / "pause" assets
    p.Circle(cx, cy, r, kOrange);
    if (playing) {
        p.Triangle(cx - r * 0.3f, cy - r * 0.45f, cx - r * 0.3f, cy + r * 0.45f, cx + r * 0.5f, cy, 0xffffff);
    } else {
        p.Round(cx - r * 0.42f, cy - r * 0.42f, r * 0.28f, r * 0.84f, r * 0.06f, 0xffffff);
        p.Round(cx + r * 0.14f, cy - r * 0.42f, r * 0.28f, r * 0.84f, r * 0.06f, 0xffffff);
    }
}

}  // namespace

void DrawPreview(HCANVAS canvas, const RECT& r, const PreviewData& d) {
#ifdef _WIN32
    if (!CoverResolver::EnsureImaging()) return;
#endif
    if (!canvas || r.right <= r.left || r.bottom <= r.top) return;
    Painter p(canvas, r);
    if (!p.Valid()) return;
    const float H = kDesignH;
    p.Round(0, 0, kDesignW, H, 8, kPanel);

    // ---- profile: activity card
    const float cx = 10, cy = 10, cw = 292, ch = H - 20;
    p.Round(cx, cy, cw, ch, 8, kCard);
    if (d.hidden) {
        Wrapped(p, d.message, cx + 14, cy + 14, cw - 28, 12.5f, kMuted, 5, 18);
    } else {
        Text(p, d.header, cx + 12, cy + 9, cw - 56, 12.5f, true, kText);
        for (int i = 0; i < 3; ++i) p.Circle(cx + cw - 30 + i * 6.5f, cy + 16, 1.6f, kText);   // "..."

        const float ix = cx + 12, iy = cy + 32, is = 64;
        if (d.cover) {
            p.Image(d.cover, ix, iy, is, is, 8, kCard);
        } else {   // the application's "aimp" image could not be loaded: a simple stand-in
            p.Round(ix, iy, is, is, 8, 0x2b2d31);
            Text(p, L"AIMP", ix + 9, iy + 23, is - 12, 15, true, 0x4fb6e0);
        }
        if (d.smallIcon) {
            const float sx = ix + is - 7, sy = iy + is - 7;
            p.Circle(sx, sy, 13, kCard);   // ring in the card colour
            if (d.icon) p.Image(d.icon, sx - 11, sy - 11, 22, 22, -1, kCard);
            else PlayIcon(p, sx, sy, 11, d.playing);
        }

        const float tx = ix + is + 12, tw = cx + cw - 12 - tx;
        Text(p, d.details, tx, iy - 1, tw, 14.5f, true, kWhite);
        Text(p, d.state, tx, iy + 19, tw, 13, false, kText);
        Text(p, d.largeText, tx, iy + 37, tw, 13, false, kText);
        const float py = iy + 56;
        if (d.progress && d.dur > 0) {
            const float frac = (float)std::min(1.0, std::max(0.0, d.pos / d.dur));
            const float bx = tx + 40, bw = tw - 80;
            Text(p, util::FormatTime(d.pos), tx, py, 38, 12, false, kText);
            Text(p, util::FormatTime(d.dur), tx + tw - 38, py, 38, 12, false, kText, true);
            p.Round(bx, py + 7, bw, 3, 1.5f, kTrack);
            if (frac > 0) p.Round(bx, py + 7, std::max(3.0f, bw * frac), 3, 1.5f, kWhite);
        } else if (!d.elapsed.empty()) {
            Text(p, d.elapsed, tx, py, tw, 12, false, kText);
        }
    }

    // ---- member list entry
    const float mx = 314, my = 10, mw = 146;
    p.Round(mx, my, mw, 48, 6, kRow);
    const float ax = mx + 22, ay = my + 24;
    if (d.avatar) p.Image(d.avatar, ax - 16, ay - 16, 32, 32, -1, kRow);
    else p.Circle(ax, ay, 16, kBlurple);
    p.Circle(ax + 11.5f, ay + 11.5f, 6.5f, kRow);
    p.Circle(ax + 11.5f, ay + 11.5f, 4.5f, kGreen);
    const float nx = mx + 44, nw = mw - 50;
    if (d.hidden || d.memberLine.empty()) {
        Text(p, d.userName, nx, my + 15, nw, 14, true, kWhite);
    } else {
        Text(p, d.userName, nx, my + 6, nw, 14, true, kWhite);
        Text(p, L"♫", nx, my + 26, 12, 12, true, kGreen);
        Text(p, d.memberLine, nx + 13, my + 26, nw - 13, 12, false, kMuted);
    }

    // ---- notes: tooltips, title link, why nothing / an example is shown
    float y = my + 58;
    for (const std::wstring* line : {&d.iconTip, &d.linkTip}) {
        if (line->empty()) continue;
        Text(p, *line, mx + 2, y, mw - 4, 11.5f, false, kMuted);
        y += 17;
    }
    if (!d.hidden && !d.message.empty()) Wrapped(p, d.message, mx + 2, y, mw - 4, 11.5f, kOrange, 3, 16);
}

// A picture with rounded corners (the author on the About tab). The corners are not painted, so the page's own
// background (whatever the skin uses) shows through. radius: in pixels of the box at 96 DPI (64 px wide box).
void DrawRoundedImage(HCANVAS canvas, const RECT& r, IAIMPImage* img, float radius) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (!canvas || !img || w <= 0 || h <= 0) return;
    const float rad = radius * w / 64.0f;
#ifdef _WIN32
    if (!CoverResolver::EnsureImaging()) return;
    // AIMP draws the picture into a bitmap, GDI+ fills the rounded shape with it (smooth edges)
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(canvas);
    HBITMAP bmp = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        RECT rc = {0, 0, w, h};
        img->Draw(mem, rc, AIMP_IMAGE_DRAW_STRETCHMODE_FILL | AIMP_IMAGE_DRAW_QUALITY_HIGH, nullptr);
        GdiFlush();
        {
            Bitmap pic(w, h, w * 4, PixelFormat32bppRGB, static_cast<BYTE*>(bits));
            Graphics g(canvas);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            TextureBrush brush(&pic);
            brush.TranslateTransform((REAL)r.left, (REAL)r.top);
            GraphicsPath path;
            AddRound(path, (REAL)r.left, (REAL)r.top, (REAL)w, (REAL)h, rad);
            g.FillPath(&brush, &path);
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
#else
    cairo_t* cr = canvas;
    cairo_save(cr);
    cairo_new_path(cr);
    const double x = r.left, y = r.top;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad, rad, -kPi / 2, 0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, kPi / 2);
    cairo_arc(cr, x + rad, y + h - rad, rad, kPi / 2, kPi);
    cairo_arc(cr, x + rad, y + rad, rad, kPi, 3 * kPi / 2);
    cairo_close_path(cr);
    cairo_clip(cr);
    img->Draw(cr, r, AIMP_IMAGE_DRAW_STRETCHMODE_FILL | AIMP_IMAGE_DRAW_QUALITY_HIGH, nullptr);
    cairo_restore(cr);
#endif
}
