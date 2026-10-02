#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <thread>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
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

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 1024, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
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
#endif

}  // namespace util
