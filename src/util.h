// Small string / JSON / URL helpers and the few OS functions shared by all modules (no AIMP SDK dependency).
// Everything platform specific (Windows / Linux) is kept behind these functions.
#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
extern HINSTANCE g_hModule;   // set in DllMain (plugin.cpp)
#endif

namespace util {

std::string  ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);

std::wstring Trim(const std::wstring& s);
std::wstring Lower(const std::wstring& s);
std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep);
std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to);
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle);

std::wstring FormatTime(double seconds);               // m:ss or h:mm:ss
std::wstring FileNameNoExt(const std::wstring& path);
std::wstring FileExt(const std::wstring& path);        // lower case, without dot
std::wstring DirName(const std::wstring& path);        // without trailing slash
bool         IsUrl(const std::wstring& path);

std::string  JsonEscape(const std::string& utf8);
std::string  JsonGetString(const std::string& json, const std::string& key);  // first match, unescaped
std::string  UrlEncode(const std::string& utf8);
std::string  TruncateUtf8(const std::string& s, size_t maxBytes);

uint64_t     Fnv1a(const std::wstring& s);
std::wstring Hex64(uint64_t v);

void Log(const wchar_t* fmt, ...);   // use %ls for wide strings (portable)

// ---- OS helpers
#ifdef _WIN32
const wchar_t kPathSep = L'\\';
#else
const wchar_t kPathSep = L'/';
#endif
uint64_t TickMs();                                         // monotonic milliseconds
void     SleepMs(unsigned ms);
FILE*    OpenFile(const std::wstring& path, const char* mode);   // mode like "rb"
bool     Seek64(FILE* f, uint64_t offset, int origin);
int64_t  Tell64(FILE* f);
bool     IsRegularFile(const std::wstring& path);
void     RemoveFile(const std::wstring& path);
void     MakeDir(const std::wstring& path);
std::wstring GetEnv(const wchar_t* name);
uint32_t ProcessId();

}  // namespace util
