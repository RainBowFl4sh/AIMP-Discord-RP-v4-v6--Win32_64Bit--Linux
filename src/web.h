// Tiny blocking HTTP(S) client (Windows: WinHTTP, Linux: libcurl loaded at run time).
#pragma once
#include <string>
#include <vector>

namespace web {

struct Response {
    int         status = 0;   // 0 = network error / aborted / too big
    std::string body;
    std::string headers;      // raw response headers (for the log when something goes wrong)
};

Response Request(const std::wstring& method, const std::wstring& url, const std::vector<std::wstring>& headers,
                 const std::string& body, size_t maxBytes = 4u << 20);
inline Response Get(const std::wstring& url, size_t maxBytes = 4u << 20) { return Request(L"GET", url, {}, {}, maxBytes); }

void Abort();   // plugin unloads: cancel running requests at once, refuse new ones
void Reset();   // plugin (re)initialized

}  // namespace web
