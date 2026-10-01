// Small string / JSON / URL helpers shared by all modules (no AIMP SDK dependency).
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

extern HINSTANCE g_hModule;   // set in DllMain (plugin.cpp)

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

void Log(const wchar_t* fmt, ...);

}  // namespace util
