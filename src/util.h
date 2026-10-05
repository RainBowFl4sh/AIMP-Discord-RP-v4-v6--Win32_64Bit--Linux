// Small string / JSON / URL helpers and the few OS functions shared by all modules (no AIMP SDK dependency).
// Everything platform specific (Windows / Linux) is kept behind these functions.
#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
extern HINSTANCE g_hModule;   // set in DllMain (plugin.cpp)
#endif

// Implemented by the plugin (plugin.cpp): refreshes the settings page soon on AIMP's main thread.
// May be called from any thread; cheap, calls are merged.
void NotifyUi();

namespace util {

std::string  ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);

std::wstring Trim(const std::wstring& s);
std::wstring Lower(const std::wstring& s);
std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep);
std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to);
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle);
bool MatchesAny(const std::wstring& text, const std::wstring& patterns);   // ';' separated substrings, no case

std::wstring FormatTime(double seconds);               // m:ss or h:mm:ss
std::wstring FileNameNoExt(const std::wstring& path);
std::wstring FileExt(const std::wstring& path);        // lower case, without dot
std::wstring DirName(const std::wstring& path);        // without trailing slash
bool         IsUrl(const std::wstring& path);
std::wstring FileUrl(const std::wstring& path);        // file:///... (opens a folder / file in the system)

std::string  JsonEscape(const std::string& utf8);
std::string  JsonGetString(const std::string& json, const std::string& key);  // first match, unescaped
std::string  JsonAfter(const std::string& json, const std::string& anchor, const std::string& key);
long long    JsonNumber(const std::string& json, const std::string& key);     // -1 if missing
std::vector<std::string> JsonObjects(const std::string& json, const std::string& arrayKey);   // flat objects
std::string  UrlEncode(const std::string& utf8);
std::string  TruncateUtf8(const std::string& s, size_t maxBytes);
bool         LooksLikeImage(const void* data, size_t size);   // JPEG / PNG / GIF / BMP / WebP signature

uint64_t     Fnv1a(const std::wstring& s);
std::wstring Hex64(uint64_t v);
std::string  Sha256Hex(const std::string& data);
std::wstring FindVersion(const std::wstring& text);    // first "1.2" / "1.2.3" in the text, empty if none
int          CompareVersions(const std::wstring& a, const std::wstring& b);   // <0, 0, >0

void Log(const wchar_t* fmt, ...);   // use %ls for wide strings (portable); kept in a small ring buffer
std::vector<std::wstring> RecentLog(size_t maxLines, uint64_t* seq = nullptr);   // newest line last
uint64_t LogSeq();                                      // changes with every new line

std::wstring Subst(std::wstring fmt, const std::wstring& a1, const std::wstring& a2 = std::wstring());  // %1 %2
std::wstring DecodeText(const std::string& bytes);      // UTF-16LE (BOM) / UTF-8 (BOM or valid) / ANSI

// key=value pairs of one [section] of an INI text (order and spelling kept); 'found' tells if the section exists
using IniPairs = std::vector<std::pair<std::wstring, std::wstring>>;
IniPairs IniSection(const std::wstring& text, const wchar_t* section, bool* found = nullptr);

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
bool     RenameFile(const std::wstring& from, const std::wstring& to);   // replaces 'to'
uint64_t FileStamp(const std::wstring& path);             // changes when the file changes, 0 = missing
int64_t  FileAgeSeconds(const std::wstring& path);        // -1 = missing

void     MakeDir(const std::wstring& path);
std::wstring GetEnv(const wchar_t* name);
uint32_t ProcessId();
int64_t  UnixTime();
std::wstring FormatDateTime(int64_t unixTime);           // local time, "YYYY-MM-DD HH:MM"
std::wstring FormatClock(int64_t unixTime);              // local time, "HH:MM:SS"
bool     UnderWine();                                      // Windows build running in Wine
bool     ReadFileBytes(const std::wstring& path, std::string& out, size_t maxBytes = 64u << 20);
bool     WriteFileBytes(const std::wstring& path, const std::string& data);   // via temp file + rename
std::vector<std::wstring> ListFiles(const std::wstring& dir, const wchar_t* ext);   // names, e.g. L".lng"
std::wstring ModuleFile();                                 // path of this plugin binary
std::wstring ModuleDir();                                  // folder of this plugin binary
std::wstring HostExe();                                    // the running AIMP executable (AppImage on Linux)
bool     Launch(const std::wstring& exe, const std::wstring& arg);   // start a program with one argument

}  // namespace util
