// Tiny blocking HTTP(S) client based on WinHTTP.
#pragma once
#include <string>
#include <vector>

namespace web {

struct Response {
    int         status = 0;   // 0 = network error
    std::string body;
};

Response Request(const std::wstring& method, const std::wstring& url,
                 const std::vector<std::wstring>& headers, const std::string& body);

}  // namespace web
