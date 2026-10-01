#include "web.h"

#include <windows.h>
#include <winhttp.h>

namespace web {

namespace {
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
}  // namespace

Response Request(const std::wstring& method, const std::wstring& url,
                 const std::vector<std::wstring>& headers, const std::string& body) {
    Response r;

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return r;

    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring object(uc.lpszUrlPath);  // path + query (rest of the string)
    if (object.empty()) object = L"/";

    Handle session;
    session.h = WinHttpOpen(L"AIMP-DiscordRPC/1.1 (AIMP plugin)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) return r;
    WinHttpSetTimeouts(session.h, 4000, 4000, 8000, 10000);

    Handle conn;
    conn.h = WinHttpConnect(session.h, host.c_str(), uc.nPort, 0);
    if (!conn.h) return r;

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    Handle req;
    req.h = WinHttpOpenRequest(conn.h, method.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req.h) return r;

    std::wstring hdr;
    for (const auto& h : headers) hdr += h + L"\r\n";

    BOOL ok = WinHttpSendRequest(req.h, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(),
                                 hdr.empty() ? 0 : (DWORD)-1L,
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (!ok || !WinHttpReceiveResponse(req.h, nullptr)) return r;

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req.h, &chunk[0], avail, &read) || read == 0) break;
        r.body.append(chunk, 0, read);
        if (r.body.size() > (4u << 20)) break;  // 4 MB safety limit
    }
    return r;
}

}  // namespace web
