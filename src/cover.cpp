#include "cover.h"

#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
using std::min;   // gdiplus.h expects these when NOMINMAX is defined
using std::max;
#include <gdiplus.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

#include "util.h"
#include "web.h"

namespace {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------- small helpers

uint32_t BE32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
uint32_t SyncSafe(const uint8_t* p) {
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) | ((uint32_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

bool ReadAt(FILE* f, uint64_t off, size_t len, Bytes& out) {
    if (!util::Seek64(f, off, SEEK_SET)) return false;
    out.resize(len);
    return len == 0 || fread(out.data(), 1, len, f) == len;
}

bool LooksLikeImage(const Bytes& b) {
    if (b.size() < 12) return false;
    if (b[0] == 0xFF && b[1] == 0xD8) return true;                       // JPEG
    if (memcmp(b.data(), "\x89PNG", 4) == 0) return true;                // PNG
    if (memcmp(b.data(), "GIF8", 4) == 0) return true;                   // GIF
    if (memcmp(b.data(), "BM", 2) == 0) return true;                     // BMP
    if (memcmp(b.data(), "RIFF", 4) == 0 && memcmp(&b[8], "WEBP", 4) == 0) return true;
    return false;
}
bool IsJpegOrPng(const Bytes& b) {
    return b.size() > 4 && ((b[0] == 0xFF && b[1] == 0xD8) || memcmp(b.data(), "\x89PNG", 4) == 0);
}

Bytes Unsync(const Bytes& in) {
    Bytes out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        out.push_back(in[i]);
        if (in[i] == 0xFF && i + 1 < in.size() && in[i + 1] == 0x00) ++i;
    }
    return out;
}

// ---------------------------------------------------------------- ID3v2 (MP3 & friends)

bool ExtractId3(FILE* f, Bytes& art) {
    Bytes h;
    if (!ReadAt(f, 0, 10, h) || memcmp(h.data(), "ID3", 3) != 0) return false;
    int ver = h[3];
    if (ver < 2 || ver > 4) return false;
    uint8_t flags = h[5];
    uint32_t size = SyncSafe(&h[6]);
    if (size == 0 || size > (48u << 20)) return false;

    Bytes tag;
    if (!ReadAt(f, 10, size, tag)) return false;
    if ((flags & 0x80) && ver < 4) tag = Unsync(tag);

    size_t pos = 0;
    if ((flags & 0x40) && ver >= 3 && tag.size() >= 4) {  // extended header
        pos = (ver == 4) ? SyncSafe(tag.data()) : (size_t)BE32(tag.data()) + 4;
    }

    int best = -1;
    const size_t hdr = (ver == 2) ? 6 : 10;
    while (pos + hdr <= tag.size()) {
        const uint8_t* fh = &tag[pos];
        if (fh[0] == 0) break;  // padding
        std::string id;
        uint32_t fsz;
        if (ver == 2) {
            id.assign((const char*)fh, 3);
            fsz = ((uint32_t)fh[3] << 16) | ((uint32_t)fh[4] << 8) | fh[5];
        } else {
            id.assign((const char*)fh, 4);
            fsz = (ver == 4) ? SyncSafe(fh + 4) : BE32(fh + 4);
        }
        uint16_t fflags = (ver >= 3) ? (uint16_t)((fh[8] << 8) | fh[9]) : 0;
        size_t dpos = pos + hdr;
        if (dpos + fsz > tag.size()) break;
        pos = dpos + fsz;

        if (!((ver == 2 && id == "PIC") || (ver >= 3 && id == "APIC"))) continue;

        Bytes data(tag.begin() + dpos, tag.begin() + dpos + fsz);
        size_t p = 0;
        if (ver == 4) {
            if (fflags & 0x0002) data = Unsync(data);
            if (fflags & 0x0001) p += 4;  // data length indicator
        }
        if (p + 1 > data.size()) continue;
        uint8_t enc = data[p++];
        if (ver == 2) {
            p += 3;  // image format "JPG"/"PNG"
        } else {
            while (p < data.size() && data[p]) ++p;  // MIME type
            ++p;
        }
        if (p >= data.size()) continue;
        uint8_t ptype = data[p++];
        if (enc == 0 || enc == 3) {                  // description, 1 byte terminator
            while (p < data.size() && data[p]) ++p;
            ++p;
        } else {                                     // UTF-16, 2 byte terminator
            while (p + 1 < data.size() && !(data[p] == 0 && data[p + 1] == 0)) p += 2;
            p += 2;
        }
        if (p >= data.size()) continue;

        int prio = (ptype == 3) ? 2 : (ptype == 0 ? 1 : 0);  // front cover > other > anything
        if (prio > best) {
            Bytes img(data.begin() + p, data.end());
            if (LooksLikeImage(img)) {
                art = std::move(img);
                best = prio;
                if (prio == 2) break;
            }
        }
    }
    return best >= 0;
}

// ---------------------------------------------------------------- FLAC

bool ExtractFlac(FILE* f, Bytes& art) {
    Bytes h;
    if (!ReadAt(f, 0, 4, h) || memcmp(h.data(), "fLaC", 4) != 0) return false;
    uint64_t pos = 4;
    int best = -1;
    for (;;) {
        Bytes bh;
        if (!ReadAt(f, pos, 4, bh)) break;
        bool last = (bh[0] & 0x80) != 0;
        int type = bh[0] & 0x7F;
        uint32_t len = ((uint32_t)bh[1] << 16) | ((uint32_t)bh[2] << 8) | bh[3];
        pos += 4;
        if (type == 6 && len > 32 && len < (48u << 20)) {
            Bytes b;
            if (!ReadAt(f, pos, len, b)) break;
            size_t p = 0;
            auto need = [&](size_t n) { return p + n <= b.size(); };
            if (need(8)) {
                uint32_t ptype = BE32(&b[p]); p += 4;
                uint32_t mlen = BE32(&b[p]); p += 4;
                if (need(mlen + 4)) {
                    p += mlen;
                    uint32_t dlen = BE32(&b[p]); p += 4;
                    if (need((size_t)dlen + 20)) {
                        p += dlen + 16;  // description + width/height/depth/colors
                        uint32_t plen = BE32(&b[p]); p += 4;
                        if (need(plen)) {
                            int prio = (ptype == 3) ? 2 : (ptype == 0 ? 1 : 0);
                            Bytes img(b.begin() + p, b.begin() + p + plen);
                            if (prio > best && LooksLikeImage(img)) {
                                art = std::move(img);
                                best = prio;
                            }
                        }
                    }
                }
            }
            if (best == 2) return true;
        }
        pos += len;
        if (last) break;
    }
    return best >= 0;
}

// ---------------------------------------------------------------- MP4 / M4A ("covr" atom)

struct Atom {
    uint64_t start = 0, body = 0, end = 0;
    char     type[5] = {0};
};

bool NextAtom(FILE* f, uint64_t pos, uint64_t limit, Atom& a) {
    Bytes h;
    if (pos + 8 > limit || !ReadAt(f, pos, 8, h)) return false;
    uint64_t size = BE32(h.data());
    uint64_t hdr = 8;
    memcpy(a.type, &h[4], 4);
    a.type[4] = 0;
    if (size == 1) {
        Bytes e;
        if (!ReadAt(f, pos + 8, 8, e)) return false;
        size = ((uint64_t)BE32(e.data()) << 32) | BE32(&e[4]);
        hdr = 16;
    } else if (size == 0) {
        size = limit - pos;
    }
    if (size < hdr || pos + size > limit) return false;
    a.start = pos;
    a.body = pos + hdr;
    a.end = pos + size;
    return true;
}

bool FindAtom(FILE* f, uint64_t from, uint64_t to, const char* type, Atom& out) {
    uint64_t pos = from;
    Atom a;
    while (NextAtom(f, pos, to, a)) {
        if (strcmp(a.type, type) == 0) { out = a; return true; }
        pos = a.end;
    }
    return false;
}

bool ExtractMp4(FILE* f, Bytes& art) {
    Bytes h;
    if (!ReadAt(f, 4, 4, h) || memcmp(h.data(), "ftyp", 4) != 0) return false;
    if (!util::Seek64(f, 0, SEEK_END)) return false;
    uint64_t fsize = (uint64_t)util::Tell64(f);

    Atom moov, udta, meta, ilst, covr;
    if (!FindAtom(f, 0, fsize, "moov", moov)) return false;
    if (!FindAtom(f, moov.body, moov.end, "udta", udta)) return false;
    if (!FindAtom(f, udta.body, udta.end, "meta", meta)) return false;
    if (!FindAtom(f, meta.body + 4, meta.end, "ilst", ilst)) return false;  // meta is a "full box": skip 4 bytes
    if (!FindAtom(f, ilst.body, ilst.end, "covr", covr)) return false;

    uint64_t pos = covr.body;
    Atom d;
    while (NextAtom(f, pos, covr.end, d)) {
        if (strcmp(d.type, "data") == 0 && d.end > d.body + 8) {
            size_t len = (size_t)(d.end - d.body - 8);
            if (len < (48u << 20)) {
                Bytes img;
                if (ReadAt(f, d.body + 8, len, img) && LooksLikeImage(img)) {
                    art = std::move(img);
                    return true;
                }
            }
        }
        pos = d.end;
    }
    return false;
}

// Sniffs the container instead of trusting the file extension.
bool ExtractEmbedded(const std::wstring& path, Bytes& art) {
    FILE* f = util::OpenFile(path, "rb");
    if (!f) return false;
    Bytes head;
    bool ok = false;
    if (ReadAt(f, 0, 12, head)) {
        if (memcmp(head.data(), "ID3", 3) == 0)                ok = ExtractId3(f, art);
        else if (memcmp(head.data(), "fLaC", 4) == 0)          ok = ExtractFlac(f, art);
        else if (memcmp(&head[4], "ftyp", 4) == 0)             ok = ExtractMp4(f, art);
    }
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------- folder images

bool ReadWholeFile(const std::wstring& path, size_t maxSize, Bytes& out) {
    FILE* f = util::OpenFile(path, "rb");
    if (!f) return false;
    util::Seek64(f, 0, SEEK_END);
    long long size = util::Tell64(f);
    bool ok = size > 0 && (unsigned long long)size <= maxSize && ReadAt(f, 0, (size_t)size, out);
    fclose(f);
    return ok;
}

bool FindFolderImage(const std::wstring& dir, const std::wstring& names, Bytes& out) {
    static const wchar_t* exts[] = {L"jpg", L"jpeg", L"png", L"bmp", L"gif"};
    if (dir.empty()) return false;
    for (const auto& raw : util::Split(names, L';')) {
        std::wstring name = util::Trim(raw);
        if (name.empty()) continue;
        for (const wchar_t* ext : exts) {
            std::wstring p = dir + util::kPathSep + name + L"." + ext;
            if (!util::IsRegularFile(p)) continue;
            if (ReadWholeFile(p, 15u << 20, out) && LooksLikeImage(out)) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- GDI+ : square 512x512 JPEG

#ifdef _WIN32

ULONG_PTR  g_gdipToken = 0;
std::mutex g_gdipMutex;

bool EnsureGdip() {
    std::lock_guard<std::mutex> lk(g_gdipMutex);
    if (g_gdipToken) return true;
    Gdiplus::GdiplusStartupInput in;
    if (Gdiplus::GdiplusStartup(&g_gdipToken, &in, nullptr) != Gdiplus::Ok) {
        g_gdipToken = 0;
        return false;
    }
    return true;
}

bool JpegClsid(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<uint8_t> buf(size);
    auto* enc = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, size, enc) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(enc[i].MimeType, L"image/jpeg") == 0) { *clsid = enc[i].Clsid; return true; }
    }
    return false;
}

Bytes ToJpeg(const Bytes& in, int target) {
    Bytes out;
    if (!EnsureGdip()) return out;
    IStream* ms = SHCreateMemStream(in.data(), (UINT)in.size());
    if (!ms) return out;
    {
        Gdiplus::Bitmap src(ms);
        if (src.GetLastStatus() == Gdiplus::Ok && src.GetWidth() > 0 && src.GetHeight() > 0) {
            int w = (int)src.GetWidth(), h = (int)src.GetHeight();
            int side = std::min(w, h);
            int sx = (w - side) / 2, sy = (h - side) / 2;  // centered square crop
            Gdiplus::Bitmap dst(target, target, PixelFormat24bppRGB);
            {
                Gdiplus::Graphics g(&dst);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(&src, Gdiplus::Rect(0, 0, target, target), sx, sy, side, side, Gdiplus::UnitPixel);
            }
            CLSID clsid;
            if (JpegClsid(&clsid)) {
                Gdiplus::EncoderParameters ep;
                ULONG quality = 88;
                ep.Count = 1;
                ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
                ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                ep.Parameter[0].NumberOfValues = 1;
                ep.Parameter[0].Value = &quality;
                IStream* os = nullptr;
                if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &os))) {
                    if (dst.Save(os, &clsid, &ep) == Gdiplus::Ok) {
                        STATSTG st = {};
                        HGLOBAL hg = nullptr;
                        if (SUCCEEDED(os->Stat(&st, STATFLAG_NONAME)) && SUCCEEDED(GetHGlobalFromStream(os, &hg))) {
                            void* p = GlobalLock(hg);
                            if (p) {
                                out.assign((uint8_t*)p, (uint8_t*)p + st.cbSize.LowPart);
                                GlobalUnlock(hg);
                            }
                        }
                    }
                    os->Release();
                }
            }
        }
    }  // bitmaps must die before the stream they read from
    ms->Release();
    return out;
}

#else
// Linux: no resizing - JPEG / PNG covers are uploaded as they are (see Resolve)
Bytes ToJpeg(const Bytes&, int) { return Bytes(); }
#endif

// ---------------------------------------------------------------- generic helpers for the online sources

std::string Quoteless(const std::wstring& s) {
    std::wstring c = util::ReplaceAll(s, L"\"", L" ");
    return util::ToUtf8(util::Trim(c));
}
std::wstring Enc(const std::string& s) { return util::FromUtf8(util::UrlEncode(s)); }

// first value of `key` that appears after `anchor`
std::string JsonAfter(const std::string& json, const std::string& anchor, const std::string& key) {
    size_t p = json.find(anchor);
    if (p == std::string::npos) return std::string();
    return util::JsonGetString(json.substr(p), key);
}
long long JsonNumber(const std::string& json, const std::string& key) {
    size_t p = json.find("\"" + key + "\"");
    if (p == std::string::npos) return -1;
    p = json.find(':', p);
    if (p == std::string::npos) return -1;
    return strtoll(json.c_str() + p + 1, nullptr, 10);
}
std::string Norm(const std::string& s) {   // lower-case, letters/digits only (UTF-8 bytes kept)
    std::string o;
    for (unsigned char c : s) {
        if (c >= 0x80) o += (char)c;
        else if (isalnum(c)) o += (char)tolower(c);
    }
    return o;
}
// guards against wrong covers: the artist of the hit must roughly match ours
bool Plausible(const std::string& found, const std::string& wanted) {
    if (wanted.empty()) return true;
    std::string a = Norm(found), b = Norm(wanted);
    if (a.empty() || b.empty()) return false;
    return a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}
std::string Https(std::string u) {
    if (u.rfind("http://", 0) == 0) u = "https://" + u.substr(7);
    return u;
}
const char* ImageExt(const Bytes& b) { return (b.size() > 4 && memcmp(b.data(), "\x89PNG", 4) == 0) ? "png" : "jpg"; }

std::string Multipart(const std::string& boundary, const std::vector<std::pair<std::string, std::string>>& fields,
                      const std::string& fileField, const Bytes& file) {
    std::string body;
    for (auto& f : fields)
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + f.first + "\"\r\n\r\n" + f.second + "\r\n";
    std::string ext = ImageExt(file);
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + fileField + "\"; filename=\"cover." + ext +
            "\"\r\nContent-Type: image/" + (ext == "png" ? "png" : "jpeg") + "\r\n\r\n";
    body.append(reinterpret_cast<const char*>(file.data()), file.size());
    body += "\r\n--" + boundary + "--\r\n";
    return body;
}

std::string Base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
    }
    return o;
}

struct Query {            // normalized track data used by all lookups
    std::string artist;   // album artist if known
    std::string trackArtist;
    std::string album;
    std::string title;
};
Query MakeQuery(const TrackInfo& t) {
    Query q;
    q.artist = Quoteless(t.albumArtist.empty() ? t.artist : t.albumArtist);
    q.trackArtist = Quoteless(t.artist);
    q.album = Quoteless(t.album);
    q.title = Quoteless(t.title);
    if (q.artist.empty()) q.artist = q.trackArtist;
    if (q.trackArtist.empty()) q.trackArtist = q.artist;
    return q;
}

// ---------------------------------------------------------------- upload hosts (local covers)

std::string CatboxUpload(const Bytes& image) {
    std::string boundary = "----AIMPDiscordRPC" + std::to_string(util::TickMs());
    std::string body = Multipart(boundary, {{"reqtype", "fileupload"}}, "fileToUpload", image);
    web::Response r = web::Request(L"POST", L"https://catbox.moe/user/api.php",
                                   {L"Content-Type: multipart/form-data; boundary=" + util::FromUtf8(boundary)}, body);
    std::string url = util::ToUtf8(util::Trim(util::FromUtf8(r.body)));
    if (r.status != 200 || url.rfind("https://", 0) != 0) {
        util::Log(L"catbox upload failed (HTTP %d)", r.status);
        return std::string();
    }
    return url;
}

std::string ImgurUpload(const Bytes& image, const std::wstring& clientId) {
    std::string boundary = "----AIMPDiscordRPC" + std::to_string(util::TickMs());
    std::string body = Multipart(boundary, {}, "image", image);
    std::vector<std::wstring> headers = {
        L"Authorization: Client-ID " + clientId,
        L"Content-Type: multipart/form-data; boundary=" + util::FromUtf8(boundary),
    };
    web::Response r = web::Request(L"POST", L"https://api.imgur.com/3/image", headers, body);
    if (r.status != 200) {
        util::Log(L"Imgur upload failed (HTTP %d)", r.status);
        return std::string();
    }
    return Https(util::JsonGetString(r.body, "link"));
}

// ---------------------------------------------------------------- Deezer (public API, no key)

std::string DeezerQuery(const std::string& endpoint, const std::string& q) {
    std::wstring url = L"https://api.deezer.com/search" + util::FromUtf8(endpoint) + L"?limit=1&q=" + Enc(q);
    web::Response r = web::Request(L"GET", url, {}, std::string());
    if (r.status != 200 || r.body.find("\"data\":[]") != std::string::npos) return std::string();
    std::string cover = util::JsonGetString(r.body, "cover_xl");
    if (cover.empty()) cover = util::JsonGetString(r.body, "cover_big");
    if (cover.find("/cover//") != std::string::npos) return std::string();  // Deezer's "no cover" placeholder
    return cover;
}

std::string DeezerLookup(const Query& q) {
    std::string url;
    if (!q.album.empty() && !q.artist.empty())
        url = DeezerQuery("/album", "artist:\"" + q.artist + "\" album:\"" + q.album + "\"");
    if (url.empty() && !q.title.empty() && !q.trackArtist.empty())
        url = DeezerQuery("/track", "artist:\"" + q.trackArtist + "\" track:\"" + q.title + "\"");
    if (url.empty() && !q.album.empty() && !q.artist.empty())
        url = DeezerQuery("/album", q.artist + " " + q.album);
    return url;
}

// ---------------------------------------------------------------- iTunes / Apple Music (public API, no key)

std::string ItunesQuery(const char* entity, const std::string& term, const std::string& artist) {
    std::wstring url = L"https://itunes.apple.com/search?media=music&limit=1&entity=" + util::FromUtf8(entity) +
                       L"&term=" + Enc(term);
    web::Response r = web::Request(L"GET", url, {}, std::string());
    if (r.status != 200) return std::string();
    if (!Plausible(util::JsonGetString(r.body, "artistName"), artist)) return std::string();
    std::string art = util::JsonGetString(r.body, "artworkUrl100");
    size_t p = art.find("100x100");
    if (p != std::string::npos) art.replace(p, 7, "600x600");
    return art;
}

std::string ItunesLookup(const Query& q) {
    std::string url;
    if (!q.album.empty() && !q.artist.empty()) url = ItunesQuery("album", q.artist + " " + q.album, q.artist);
    if (url.empty() && !q.title.empty() && !q.trackArtist.empty())
        url = ItunesQuery("song", q.trackArtist + " " + q.title, q.trackArtist);
    return url;
}

// ---------------------------------------------------------------- Bandcamp (public search endpoint, no key)

std::string BandcampQuery(const char* filter, const std::string& text, const std::string& artist) {
    std::string body = "{\"search_text\":\"" + util::JsonEscape(text) + "\",\"search_filter\":\"" + filter +
                       "\",\"full_page\":false,\"fan_id\":null}";
    web::Response r = web::Request(L"POST", L"https://bandcamp.com/api/bcsearch_public_api/1/autocomplete_elastic",
                                   {L"Content-Type: application/json"}, body);
    if (r.status != 200) return std::string();
    if (!Plausible(util::JsonGetString(r.body, "band_name"), artist)) return std::string();
    std::string img = util::JsonGetString(r.body, "img");          // ..._3.jpg (100px)
    size_t us = img.rfind('_'), dot = img.rfind('.');
    if (img.empty() || us == std::string::npos || dot == std::string::npos || dot < us) return std::string();
    return Https(img.substr(0, us) + "_10" + img.substr(dot));     // _10 = full size
}

std::string BandcampLookup(const Query& q) {
    std::string url;
    if (!q.album.empty()) url = BandcampQuery("a", q.artist + " " + q.album, q.artist);
    if (url.empty() && !q.title.empty()) url = BandcampQuery("t", q.trackArtist + " " + q.title, q.trackArtist);
    return url;
}

// ---------------------------------------------------------------- MusicBrainz + Cover Art Archive (no key)

std::string MusicBrainzLookup(const Query& q) {
    if (q.artist.empty() || (q.album.empty() && q.title.empty())) return std::string();
    std::string lucene;
    std::wstring url;
    if (!q.album.empty()) {
        lucene = "releasegroup:\"" + q.album + "\" AND artist:\"" + q.artist + "\"";
        url = L"https://musicbrainz.org/ws/2/release-group/?fmt=json&limit=1&query=" + Enc(lucene);
    } else {
        lucene = "recording:\"" + q.title + "\" AND artist:\"" + q.trackArtist + "\"";
        url = L"https://musicbrainz.org/ws/2/recording/?fmt=json&limit=1&query=" + Enc(lucene);
    }
    web::Response r = web::Request(L"GET", url, {L"Accept: application/json"}, std::string());
    if (r.status != 200 || JsonNumber(r.body, "score") < 90) return std::string();

    std::string caa;
    if (!q.album.empty()) {
        std::string id = JsonAfter(r.body, "\"release-groups\"", "id");
        if (!id.empty()) caa = "https://coverartarchive.org/release-group/" + id;
    } else {
        std::string id = JsonAfter(r.body, "\"releases\"", "id");
        if (!id.empty()) caa = "https://coverartarchive.org/release/" + id;
    }
    if (caa.empty()) return std::string();
    web::Response c = web::Request(L"GET", util::FromUtf8(caa), {L"Accept: application/json"}, std::string());
    if (c.status != 200) return std::string();
    std::string img = JsonAfter(c.body, "\"thumbnails\"", "500");
    if (img.empty()) img = JsonAfter(c.body, "\"thumbnails\"", "large");
    return Https(img);
}

// ---------------------------------------------------------------- Discogs (personal access token)

std::string DiscogsLookup(const Query& q, const std::wstring& token) {
    if (token.empty() || q.artist.empty()) return std::string();
    std::wstring url = L"https://api.discogs.com/database/search?type=release&per_page=1&artist=" + Enc(q.artist);
    if (!q.album.empty()) url += L"&release_title=" + Enc(q.album);
    else if (!q.title.empty()) url += L"&track=" + Enc(q.title);
    else return std::string();
    url += L"&token=" + Enc(util::ToUtf8(token));
    web::Response r = web::Request(L"GET", url, {}, std::string());
    if (r.status != 200) {
        if (r.status) util::Log(L"Discogs lookup failed (HTTP %d)", r.status);
        return std::string();
    }
    if (!Plausible(util::JsonGetString(r.body, "title"), q.artist)) return std::string();  // "Artist - Album"
    std::string img = util::JsonGetString(r.body, "cover_image");
    if (img.find("spacer.gif") != std::string::npos) return std::string();
    return Https(img);
}

// ---------------------------------------------------------------- Spotify (client credentials)

std::mutex   g_spMu;
std::string  g_spToken;
std::wstring g_spFor;
uint64_t     g_spExpires = 0;

std::string SpotifyToken(const std::wstring& id, const std::wstring& secret, bool forceNew) {
    std::lock_guard<std::mutex> lk(g_spMu);
    std::wstring who = id + L":" + secret;
    if (!forceNew && !g_spToken.empty() && g_spFor == who && util::TickMs() < g_spExpires) return g_spToken;
    g_spToken.clear();
    web::Response r = web::Request(L"POST", L"https://accounts.spotify.com/api/token",
                                   {L"Authorization: Basic " + util::FromUtf8(Base64(util::ToUtf8(who))),
                                    L"Content-Type: application/x-www-form-urlencoded"},
                                   "grant_type=client_credentials");
    if (r.status != 200) {
        util::Log(L"Spotify token request failed (HTTP %d)", r.status);
        return std::string();
    }
    g_spToken = util::JsonGetString(r.body, "access_token");
    long long sec = JsonNumber(r.body, "expires_in");
    g_spExpires = util::TickMs() + (uint64_t)((sec > 120 ? sec - 60 : 3000) * 1000);
    g_spFor = who;
    return g_spToken;
}

std::string SpotifySearch(const std::wstring& id, const std::wstring& secret, const char* type, const std::string& q,
                          const std::string& artist) {
    std::wstring url = L"https://api.spotify.com/v1/search?limit=1&type=" + util::FromUtf8(type) + L"&q=" + Enc(q);
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string token = SpotifyToken(id, secret, attempt > 0);
        if (token.empty()) return std::string();
        web::Response r = web::Request(L"GET", url, {L"Authorization: Bearer " + util::FromUtf8(token)}, std::string());
        if (r.status == 401) continue;  // expired token -> fetch a new one once
        if (r.status != 200) return std::string();
        if (!Plausible(JsonAfter(r.body, "\"artists\"", "name"), artist)) return std::string();
        return JsonAfter(r.body, "\"images\"", "url");    // first image = largest (640 px)
    }
    return std::string();
}

std::string SpotifyLookup(const Query& q, const Config& cfg) {
    if (cfg.spotifyId.empty() || cfg.spotifySecret.empty()) return std::string();
    std::string url;
    if (!q.album.empty() && !q.artist.empty())
        url = SpotifySearch(cfg.spotifyId, cfg.spotifySecret, "album", "album:" + q.album + " artist:" + q.artist, q.artist);
    if (url.empty() && !q.title.empty() && !q.trackArtist.empty())
        url = SpotifySearch(cfg.spotifyId, cfg.spotifySecret, "track", "track:" + q.title + " artist:" + q.trackArtist,
                            q.trackArtist);
    return url;
}

// ---------------------------------------------------------------- cache key

std::string KeyOf(const TrackInfo& t) {
    std::wstring key;
    std::wstring a = util::Lower(t.albumArtist.empty() ? t.artist : t.albumArtist);
    if (!t.album.empty())
        key = L"a|" + a + L"|" + util::Lower(t.album);
    else if (!t.fileName.empty() && !util::IsUrl(t.fileName))
        key = L"f|" + util::Lower(t.fileName);
    else
        key = L"t|" + util::Lower(t.artist) + L"|" + util::Lower(t.title);
    char buf[24];
    snprintf(buf, sizeof buf, "%016llx", (unsigned long long)util::Fnv1a(key));
    return buf;
}

}  // namespace

// ================================================================== CoverResolver

namespace {
#ifdef _WIN32
std::filesystem::path FsPath(const std::wstring& p) { return std::filesystem::path(p); }
#else
std::string FsPath(const std::wstring& p) { return util::ToUtf8(p); }
#endif
}  // namespace

CoverResolver::CoverResolver() {
    path_ = config::DataDir() + util::kPathSep + L"DiscordRPC_covers.tsv";
    LoadCache();
}

void CoverResolver::LoadCache() {
    std::lock_guard<std::mutex> lk(mu_);
    std::ifstream in(FsPath(path_), std::ios::binary);
    if (!in) return;
    std::vector<std::pair<std::string, std::string>> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
        lines.emplace_back(line.substr(0, tab), line.substr(tab + 1));
    }
    in.close();

    if (lines.size() > 3000) {  // keep the file small: retain the newest 2000 entries
        lines.erase(lines.begin(), lines.end() - 2000);
        std::ofstream out(FsPath(path_), std::ios::binary | std::ios::trunc);
        for (auto& l : lines) out << l.first << '\t' << l.second << '\n';
    }
    for (auto& l : lines) cache_[l.first] = l.second;
}

void CoverResolver::StoreCache(const std::string& key, const std::string& url) {
    std::lock_guard<std::mutex> lk(mu_);
    cache_[key] = url;
    negative_.erase(key);
    std::ofstream out(FsPath(path_), std::ios::binary | std::ios::app);
    if (out) out << key << '\t' << url << '\n';
}

void CoverResolver::ClearCache() {
    std::lock_guard<std::mutex> lk(mu_);
    cache_.clear();
    negative_.clear();
    util::RemoveFile(path_);
}

std::string CoverResolver::PeekCache(const TrackInfo& t) {
    std::string key = KeyOf(t);
    std::lock_guard<std::mutex> lk(mu_);
    auto it = cache_.find(key);
    return it == cache_.end() ? std::string() : it->second;
}

std::string CoverResolver::Resolve(const TrackInfo& t, const Config& cfg) {
    if (!cfg.coverEnabled) return std::string();
    const std::string key = KeyOf(t);
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        auto n = negative_.find(key);
        if (n != negative_.end() && util::TickMs() - n->second < 10ull * 60 * 1000) return std::string();
    }

    std::string url;
    const Query q = MakeQuery(t);

    // a) local cover (tags / folder) -> public upload host
    auto uploadLocal = [&]() -> std::string {
        const bool isLocalFile = !t.fileName.empty() && !util::IsUrl(t.fileName);
        if (!isLocalFile || cfg.uploadHost == 0 || !(cfg.srcEmbedded || cfg.srcFolder)) return std::string();
        if (cfg.uploadHost == 2 && cfg.imgurClientId.empty()) return std::string();
        Bytes art;
        if (cfg.srcEmbedded) ExtractEmbedded(t.fileName, art);
        if (art.empty() && cfg.srcFolder) FindFolderImage(util::DirName(t.fileName), cfg.coverNames, art);
        if (art.empty()) return std::string();
        Bytes upload = ToJpeg(art, 512);
        if (upload.empty() && IsJpegOrPng(art) && art.size() <= (5u << 20)) upload = art;  // GDI+ failed / Linux: raw
        if (upload.empty()) return std::string();
        std::string u = (cfg.uploadHost == 2) ? ImgurUpload(upload, cfg.imgurClientId) : CatboxUpload(upload);
        if (!u.empty()) util::Log(L"Cover uploaded: %ls", util::FromUtf8(u).c_str());
        return u;
    };

    // b) public lookups - first hit wins
    auto lookupOnline = [&]() -> std::string {
        struct Src { bool on; const wchar_t* name; std::function<std::string()> fn; };
        const Src sources[] = {
            {cfg.srcSpotify,     L"Spotify",     [&] { return SpotifyLookup(q, cfg); }},
            {cfg.srcDeezer,      L"Deezer",      [&] { return DeezerLookup(q); }},
            {cfg.srcItunes,      L"iTunes",      [&] { return ItunesLookup(q); }},
            {cfg.srcBandcamp,    L"Bandcamp",    [&] { return BandcampLookup(q); }},
            {cfg.srcDiscogs,     L"Discogs",     [&] { return DiscogsLookup(q, cfg.discogsToken); }},
            {cfg.srcMusicBrainz, L"MusicBrainz", [&] { return MusicBrainzLookup(q); }},
        };
        for (const Src& s : sources) {
            if (!s.on) continue;
            std::string u = s.fn();
            if (!u.empty() && u.size() <= 256) {
                util::Log(L"Cover found on %ls", s.name);
                return u;
            }
        }
        return std::string();
    };

    if (cfg.preferLocal) url = uploadLocal();
    if (url.empty()) url = lookupOnline();
    if (url.empty() && !cfg.preferLocal) url = uploadLocal();

    if (url.size() > 256) url.clear();  // Discord limit for image keys / URLs

    if (!url.empty()) {
        StoreCache(key, url);
    } else {
        std::lock_guard<std::mutex> lk(mu_);
        negative_[key] = util::TickMs();
    }
    return url;
}

void CoverResolver::ShutdownImaging() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_gdipMutex);
    if (g_gdipToken) {
        Gdiplus::GdiplusShutdown(g_gdipToken);
        g_gdipToken = 0;
    }
#endif
}
