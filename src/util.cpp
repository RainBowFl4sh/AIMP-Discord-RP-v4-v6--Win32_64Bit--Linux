#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <dirent.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace util {

static void AppendUtf8(std::string& out, uint32_t cp);

#ifdef _WIN32
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
#else
// wchar_t is UTF-32 on Linux
std::string ToUtf8(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) {
        uint32_t cp = (uint32_t)c;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) cp = 0xFFFD;
        AppendUtf8(s, cp);
    }
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int extra;
        if (c < 0x80)              { cp = c;        extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { w += (wchar_t)0xFFFD; ++i; continue; }
        if (i + extra >= s.size()) { w += (wchar_t)0xFFFD; break; }   // truncated sequence
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { w += (wchar_t)0xFFFD; ++i; continue; }
        w += (wchar_t)cp;
        i += 1 + extra;
    }
    return w;
}
#endif

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring Lower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return r;
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::wstring::npos ? p : p - start));
        if (p == std::wstring::npos) break;
        start = p + 1;
    }
    return out;
}

std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    return Lower(haystack).find(Lower(needle)) != std::wstring::npos;
}

bool MatchesAny(const std::wstring& text, const std::wstring& patterns) {
    if (text.empty() || patterns.empty()) return false;
    const std::wstring t = Lower(text);
    for (const auto& raw : Split(patterns, L';')) {
        std::wstring p = Lower(Trim(raw));
        if (!p.empty() && t.find(p) != std::wstring::npos) return true;
    }
    return false;
}

std::wstring FormatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = (int)(seconds + 0.5);
    int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    wchar_t buf[32];
    if (h > 0) swprintf(buf, 32, L"%d:%02d:%02d", h, m, s);
    else       swprintf(buf, 32, L"%d:%02d", m, s);
    return buf;
}

static size_t LastSep(const std::wstring& p) {
    size_t a = p.find_last_of(L'\\');
    size_t b = p.find_last_of(L'/');
    if (a == std::wstring::npos) return b;
    if (b == std::wstring::npos) return a;
    return std::max(a, b);
}

std::wstring FileNameNoExt(const std::wstring& path) {
    size_t sep = LastSep(path);
    std::wstring name = (sep == std::wstring::npos) ? path : path.substr(sep + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

std::wstring FileExt(const std::wstring& path) {
    size_t sep = LastSep(path);
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return L"";
    return Lower(path.substr(dot + 1));
}

std::wstring DirName(const std::wstring& path) {
    size_t sep = LastSep(path);
    return sep == std::wstring::npos ? std::wstring() : path.substr(0, sep);
}

bool IsUrl(const std::wstring& path) {
    std::wstring l = Lower(path.substr(0, 8));
    return l.rfind(L"http://", 0) == 0 || l.rfind(L"https://", 0) == 0 || l.rfind(L"mms://", 0) == 0 ||
           l.rfind(L"rtmp://", 0) == 0 || l.rfind(L"ftp://", 0) == 0;
}

std::wstring FileUrl(const std::wstring& path) {
    std::string u = "file://";
    if (!path.empty() && path[0] != L'/') u += '/';   // C:/...
    for (unsigned char c : ToUtf8(path)) {
        if (c == '\\') c = '/';
        if (isalnum(c) || strchr("/:-_.~", c)) {
            u += (char)c;
        } else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            u += b;
        }
    }
    return FromUtf8(u);
}

std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o;
}

static void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    }
}

static int Hex4(const std::string& s, size_t i, uint32_t& v) {
    if (i + 4 > s.size()) return 0;
    v = 0;
    for (size_t k = 0; k < 4; ++k) {
        char c = s[i + k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    return 1;
}

std::string JsonGetString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle);
    while (p != std::string::npos) {
        size_t i = p + needle.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
            if (i < json.size() && json[i] == '"') {
                ++i;
                std::string out;
                while (i < json.size() && json[i] != '"') {
                    char c = json[i];
                    if (c == '\\' && i + 1 < json.size()) {
                        char e = json[++i];
                        switch (e) {
                            case 'n': out += '\n'; break;
                            case 'r': out += '\r'; break;
                            case 't': out += '\t'; break;
                            case 'b': out += '\b'; break;
                            case 'f': out += '\f'; break;
                            case 'u': {
                                uint32_t cp = 0;
                                if (Hex4(json, i + 1, cp)) {
                                    i += 4;
                                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < json.size() && json[i + 1] == '\\' && json[i + 2] == 'u') {
                                        uint32_t lo = 0;
                                        if (Hex4(json, i + 3, lo) && lo >= 0xDC00 && lo < 0xE000) {
                                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                            i += 6;
                                        }
                                    }
                                    AppendUtf8(out, cp);
                                }
                                break;
                            }
                            default: out += e;  // covers \" \\ \/
                        }
                    } else {
                        out += c;
                    }
                    ++i;
                }
                return out;
            }
        }
        p = json.find(needle, p + 1);
    }
    return std::string();
}

std::string JsonAfter(const std::string& json, const std::string& anchor, const std::string& key) {
    size_t p = json.find(anchor);
    return p == std::string::npos ? std::string() : JsonGetString(json.substr(p), key);
}

long long JsonNumber(const std::string& json, const std::string& key) {
    size_t p = json.find("\"" + key + "\"");
    if (p == std::string::npos) return -1;
    p = json.find(':', p);
    if (p == std::string::npos) return -1;
    return strtoll(json.c_str() + p + 1, nullptr, 10);
}

std::vector<std::string> JsonObjects(const std::string& json, const std::string& arrayKey) {
    std::vector<std::string> out;
    size_t p = 0;
    if (!arrayKey.empty() && (p = json.find("\"" + arrayKey + "\"")) == std::string::npos) return out;
    if ((p = json.find('[', p)) == std::string::npos) return out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = p + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') {   // skip strings (they may contain brackets)
            for (++i; i < json.size() && json[i] != '"'; ++i)
                if (json[i] == '\\') ++i;
        } else if (c == '{' || c == '[') {
            if (depth++ == 0) start = i;
        } else if (c == '}' || c == ']') {
            if (depth == 0) break;   // end of the array
            if (--depth == 0 && c == '}') out.push_back(json.substr(start, i - start + 1));
        }
    }
    return out;
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            o += (char)c;
        } else {
            o += '%'; o += hex[c >> 4]; o += hex[c & 15];
        }
    }
    return o;
}

std::string TruncateUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes > 3 ? maxBytes - 3 : 0;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\xE2\x80\xA6";  // ellipsis
}

bool LooksLikeImage(const void* data, size_t size) {
    const unsigned char* b = static_cast<const unsigned char*>(data);
    if (size < 12) return false;
    return (b[0] == 0xFF && b[1] == 0xD8) || memcmp(b, "\x89PNG", 4) == 0 || memcmp(b, "GIF8", 4) == 0 ||
           memcmp(b, "BM", 2) == 0 || (memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0);
}

uint64_t Fnv1a(const std::wstring& s) {
    uint64_t h = 1469598103934665603ULL;
    for (wchar_t c : s) {
        h ^= (uint64_t)(uint16_t)c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::wstring Hex64(uint64_t v) {
    wchar_t b[24];
    swprintf(b, 24, L"%016llx", (unsigned long long)v);
    return b;
}

std::string Sha256Hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
        0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
        0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    std::string tail = data.substr(data.size() & ~(size_t)63);   // last partial block + padding
    tail += '\x80';
    while (tail.size() % 64 != 56) tail += '\0';
    for (int i = 7; i >= 0; --i) tail += (char)(((uint64_t)data.size() * 8) >> (i * 8));
    const size_t full = data.size() & ~(size_t)63;
    for (size_t off = 0; off < full + tail.size(); off += 64) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(off < full ? &data[off] : &tail[off - full]);
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i)
            w[i] = w[i - 16] + (rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
                   (rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10));
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

std::wstring FindVersion(const std::wstring& t) {
    for (size_t i = 0; i < t.size(); ++i) {
        if (!iswdigit(t[i]) || (i > 0 && (iswdigit(t[i - 1]) || t[i - 1] == L'.'))) continue;
        size_t j = i, dots = 0;
        while (j < t.size() && (iswdigit(t[j]) || (t[j] == L'.' && j + 1 < t.size() && iswdigit(t[j + 1]) && ++dots)))
            ++j;
        if (dots > 0) return t.substr(i, j - i);
    }
    return std::wstring();
}

int CompareVersions(const std::wstring& a, const std::wstring& b) {
    std::vector<std::wstring> x = Split(a, L'.'), y = Split(b, L'.');
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        long p = i < x.size() ? wcstol(x[i].c_str(), nullptr, 10) : 0;
        long q = i < y.size() ? wcstol(y[i].c_str(), nullptr, 10) : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

namespace {
std::mutex               g_logMu;
std::deque<std::wstring> g_log;   // last lines for the diagnostics tab
uint64_t                 g_logSeq = 0;
}  // namespace

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 1024, fmt, ap);
    va_end(ap);
    buf[1023] = 0;

    time_t t = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    wchar_t stamp[16];
    swprintf(stamp, 16, L"%02d:%02d:%02d ", lt.tm_hour, lt.tm_min, lt.tm_sec);
    {
        std::lock_guard<std::mutex> lk(g_logMu);
        g_log.push_back(std::wstring(stamp) + buf);
        if (g_log.size() > 60) g_log.pop_front();
        ++g_logSeq;
    }
    std::wstring line = L"[AIMP DiscordRPC] ";
    line += buf;
    line += L"\n";
#ifdef _WIN32
    OutputDebugStringW(line.c_str());
#else
    static const bool enabled = getenv("AIMP_DISCORD_RPC_DEBUG") != nullptr;
    if (enabled) fputs(ToUtf8(line).c_str(), stderr);
#endif
}

std::vector<std::wstring> RecentLog(size_t maxLines, uint64_t* seq) {
    std::lock_guard<std::mutex> lk(g_logMu);
    size_t n = std::min(maxLines, g_log.size());
    if (seq) *seq = g_logSeq;
    return std::vector<std::wstring>(g_log.end() - (ptrdiff_t)n, g_log.end());
}

uint64_t LogSeq() {
    std::lock_guard<std::mutex> lk(g_logMu);
    return g_logSeq;
}

std::wstring Subst(std::wstring fmt, const std::wstring& a1, const std::wstring& a2) {
    fmt = ReplaceAll(std::move(fmt), L"%1", a1);
    return ReplaceAll(std::move(fmt), L"%2", a2);
}

std::wstring DecodeText(const std::string& b) {
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE) {   // UTF-16LE
        std::wstring w;
        std::u16string u;
        for (size_t i = 2; i + 1 < b.size(); i += 2) u += (char16_t)((unsigned char)b[i] | ((unsigned char)b[i + 1] << 8));
#ifdef _WIN32
        w.assign(u.begin(), u.end());
#else
        for (size_t i = 0; i < u.size(); ++i) {   // surrogate pairs -> UTF-32
            uint32_t c = u[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000)
                c = 0x10000 + ((c - 0xD800) << 10) + (u[++i] - 0xDC00);
            w += (wchar_t)c;
        }
#endif
        return w;
    }
    size_t start = (b.size() >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB &&
                    (unsigned char)b[2] == 0xBF) ? 3 : 0;
    std::string body = b.substr(start);
    // valid UTF-8? (ASCII is valid UTF-8 too)
    bool valid = true;
    for (size_t i = 0; i < body.size() && valid;) {
        unsigned char c = (unsigned char)body[i];
        size_t n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9;
        if (n == 9 || (n > 0 && i + n >= body.size())) { valid = false; break; }   // bad lead byte / truncated
        for (size_t k = 1; k <= n && valid; ++k) valid = ((unsigned char)body[i + k] & 0xC0) == 0x80;
        i += 1 + n;
    }
    if (valid || start) return FromUtf8(body);
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_ACP, 0, body.data(), (int)body.size(), nullptr, 0);
    std::wstring w((size_t)std::max(n, 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, body.data(), (int)body.size(), &w[0], n);
    return w;
#else
    std::wstring w;
    for (unsigned char c : body) w += (wchar_t)c;   // Latin-1
    return w;
#endif
}

IniPairs IniSection(const std::wstring& text, const wchar_t* section, bool* found) {
    IniPairs out;
    const std::wstring want = Lower(section);
    bool in = false, any = false;
    for (size_t pos = 0; pos < text.size();) {
        size_t nl = text.find(L'\n', pos);
        std::wstring line = Trim(text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
        pos = nl == std::wstring::npos ? text.size() : nl + 1;
        if (!line.empty() && line[0] == 0xFEFF) line.erase(0, 1);   // BOM left by an editor
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            size_t e = line.find(L']');
            in = e != std::wstring::npos && Lower(Trim(line.substr(1, e - 1))) == want;
            any = any || in;
            continue;
        }
        size_t eq = line.find(L'=');
        if (!in || eq == std::wstring::npos || eq == 0) continue;
        std::wstring v = Trim(line.substr(eq + 1));
        if (v.size() >= 2 && (v[0] == L'"' || v[0] == L'\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
        out.emplace_back(Trim(line.substr(0, eq)), v);
    }
    if (found) *found = any;
    return out;
}

// ---------------------------------------------------------------- OS helpers

uint64_t TickMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void SleepMs(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#ifdef _WIN32
FILE* OpenFile(const std::wstring& path, const char* mode) {
    std::wstring m(mode, mode + strlen(mode));
    return _wfopen(path.c_str(), m.c_str());
}
bool    Seek64(FILE* f, uint64_t offset, int origin) { return _fseeki64(f, (long long)offset, origin) == 0; }
int64_t Tell64(FILE* f) { return _ftelli64(f); }
bool IsRegularFile(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
void RemoveFile(const std::wstring& path) { DeleteFileW(path.c_str()); }
void MakeDir(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }
std::wstring GetEnv(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return std::wstring();
    std::wstring v(n, L'\0');
    n = GetEnvironmentVariableW(name, &v[0], n);
    v.resize(n);
    return v;
}
uint32_t ProcessId() { return (uint32_t)GetCurrentProcessId(); }
bool RenameFile(const std::wstring& from, const std::wstring& to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
std::vector<std::wstring> ListFiles(const std::wstring& dir, const wchar_t* ext) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*" + ext).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}
uint64_t FileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return 0;
    uint64_t t = ((uint64_t)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    return t ^ ((uint64_t)fa.nFileSizeLow << 40) ^ 1;
}
int64_t FileAgeSeconds(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return -1;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    uint64_t a = ((uint64_t)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    uint64_t b = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return b > a ? (int64_t)((b - a) / 10000000ull) : 0;
}
std::wstring HostExe() {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}
bool Launch(const std::wstring& exe, const std::wstring& arg) {
    std::wstring cmd = L"\"" + exe + L"\" \"" + arg + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
std::wstring ModuleFile() {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameW(g_hModule, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}
#else
FILE*   OpenFile(const std::wstring& path, const char* mode) { return fopen(ToUtf8(path).c_str(), mode); }
bool    Seek64(FILE* f, uint64_t offset, int origin) { return fseeko(f, (off_t)offset, origin) == 0; }
int64_t Tell64(FILE* f) { return (int64_t)ftello(f); }
bool IsRegularFile(const std::wstring& path) {
    struct stat st;
    return stat(ToUtf8(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
void RemoveFile(const std::wstring& path) { unlink(ToUtf8(path).c_str()); }
void MakeDir(const std::wstring& path) { mkdir(ToUtf8(path).c_str(), 0700); }
std::wstring GetEnv(const wchar_t* name) {
    const char* v = getenv(ToUtf8(name).c_str());
    return v ? FromUtf8(v) : std::wstring();
}
uint32_t ProcessId() { return (uint32_t)getpid(); }
bool RenameFile(const std::wstring& from, const std::wstring& to) {
    return rename(ToUtf8(from).c_str(), ToUtf8(to).c_str()) == 0;
}
std::vector<std::wstring> ListFiles(const std::wstring& dir, const wchar_t* ext) {
    std::vector<std::wstring> out;
    DIR* d = opendir(ToUtf8(dir).c_str());
    if (!d) return out;
    std::wstring e = Lower(ext);
    while (dirent* de = readdir(d)) {
        std::wstring n = FromUtf8(de->d_name);
        if (n.size() > e.size() && Lower(n.substr(n.size() - e.size())) == e && IsRegularFile(dir + L"/" + n))
            out.push_back(n);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}
uint64_t FileStamp(const std::wstring& path) {
    struct stat st;
    if (stat(ToUtf8(path).c_str(), &st) != 0) return 0;
    return ((uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec) ^ ((uint64_t)st.st_size << 40) ^ 1;
}
int64_t FileAgeSeconds(const std::wstring& path) {
    struct stat st;
    if (stat(ToUtf8(path).c_str(), &st) != 0) return -1;
    int64_t age = (int64_t)time(nullptr) - (int64_t)st.st_mtime;
    return age > 0 ? age : 0;
}
std::wstring HostExe() {
    std::wstring appImage = GetEnv(L"APPIMAGE");   // AIMP started as AppImage: run the AppImage again
    if (!appImage.empty()) return appImage;
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? FromUtf8(std::string(buf, (size_t)n)) : std::wstring();
}
bool Launch(const std::wstring& exe, const std::wstring& arg) {
    // through a short-lived shell that puts the program in the background: no zombie process is left behind
    std::string e = ToUtf8(exe), a = ToUtf8(arg);
    const char* argv[] = {"sh", "-c", "\"$0\" \"$1\" >/dev/null 2>&1 &", e.c_str(), a.c_str(), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
std::wstring ModuleFile() {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&ModuleFile), &info) && info.dli_fname) return FromUtf8(info.dli_fname);
    return std::wstring();
}
#endif

std::wstring ModuleDir() {
    const std::wstring file = ModuleFile();
    return file.empty() ? L"." : DirName(file);
}

int64_t UnixTime() { return (int64_t)time(nullptr); }

std::wstring FormatDateTime(int64_t unixTime) {
    time_t t = (time_t)unixTime;
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    wchar_t b[32];
    swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    return b;
}

std::wstring FormatClock(int64_t unixTime) {
    std::wstring dt = FormatDateTime(unixTime);   // "YYYY-MM-DD HH:MM"
    time_t t = (time_t)unixTime;
    wchar_t b[8];
    swprintf(b, 8, L":%02d", (int)(t % 60));
    return dt.substr(11) + b;
}

bool UnderWine() {
#ifdef _WIN32
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

bool ReadFileBytes(const std::wstring& path, std::string& out, size_t maxBytes) {
    out.clear();
    FILE* f = OpenFile(path, "rb");
    if (!f) return false;
    char buf[16384];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        if (out.size() + n > maxBytes) { ok = false; break; }
        out.append(buf, n);
    }
    fclose(f);
    return ok;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    FILE* f = OpenFile(tmp, "wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok || !RenameFile(tmp, path)) {
        RemoveFile(tmp);
        return false;
    }
    return true;
}

}  // namespace util
