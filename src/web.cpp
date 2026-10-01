#include "web.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#define CURL_DISABLE_TYPECHECK
#include <curl/curl.h>
#include <dlfcn.h>
#include "util.h"
#endif

namespace web {

#ifdef _WIN32

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

#else  // Linux: libcurl, loaded at run time so the plugin works with every distribution's libcurl build
       // (and still loads without it - covers are then simply not looked up)

namespace {

struct CurlApi {
    decltype(&curl_global_init)    global_init = nullptr;
    decltype(&curl_easy_init)      easy_init = nullptr;
    decltype(&curl_easy_setopt)    easy_setopt = nullptr;
    decltype(&curl_easy_perform)   easy_perform = nullptr;
    decltype(&curl_easy_getinfo)   easy_getinfo = nullptr;
    decltype(&curl_easy_cleanup)   easy_cleanup = nullptr;
    decltype(&curl_slist_append)   slist_append = nullptr;
    decltype(&curl_slist_free_all) slist_free_all = nullptr;
    bool ok = false;
};

const CurlApi& Curl() {
    static const CurlApi api = [] {
        CurlApi a;
        void* h = nullptr;
        for (const char* name : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl-nss.so.4", "libcurl.so"})
            if ((h = dlopen(name, RTLD_NOW | RTLD_LOCAL)) != nullptr) break;
        if (!h) {
            util::Log(L"libcurl not found - online cover lookup / upload disabled");
            return a;
        }
#define LOAD(field, sym) a.field = reinterpret_cast<decltype(a.field)>(dlsym(h, #sym))
        LOAD(global_init, curl_global_init);
        LOAD(easy_init, curl_easy_init);
        LOAD(easy_setopt, curl_easy_setopt);
        LOAD(easy_perform, curl_easy_perform);
        LOAD(easy_getinfo, curl_easy_getinfo);
        LOAD(easy_cleanup, curl_easy_cleanup);
        LOAD(slist_append, curl_slist_append);
        LOAD(slist_free_all, curl_slist_free_all);
#undef LOAD
        a.ok = a.global_init && a.easy_init && a.easy_setopt && a.easy_perform && a.easy_getinfo && a.easy_cleanup &&
               a.slist_append && a.slist_free_all;
        if (a.ok) a.global_init(CURL_GLOBAL_DEFAULT);   // thread-safe here: static initialization runs once
        return a;
    }();
    return api;
}

size_t OnData(char* p, size_t size, size_t n, void* user) {
    std::string* body = static_cast<std::string*>(user);
    size_t len = size * n;
    if (body->size() + len > (4u << 20)) return 0;  // 4 MB safety limit -> abort transfer
    body->append(p, len);
    return len;
}

}  // namespace

Response Request(const std::wstring& method, const std::wstring& url,
                 const std::vector<std::wstring>& headers, const std::string& body) {
    Response r;
    const CurlApi& api = Curl();
    if (!api.ok) return r;
    CURL* c = api.easy_init();
    if (!c) return r;

    const std::string u = util::ToUtf8(url), m = util::ToUtf8(method);
    curl_slist* hl = nullptr;
    for (const auto& h : headers) hl = api.slist_append(hl, util::ToUtf8(h).c_str());

    api.easy_setopt(c, CURLOPT_URL, u.c_str());
    api.easy_setopt(c, CURLOPT_USERAGENT, "AIMP-DiscordRPC/1.1 (AIMP plugin)");
    api.easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    api.easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    api.easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
    api.easy_setopt(c, CURLOPT_TIMEOUT_MS, 20000L);
    api.easy_setopt(c, CURLOPT_NOSIGNAL, 1L);          // we run on a worker thread
    api.easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");    // gzip etc.
    if (hl) api.easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    if (m == "POST") {
        api.easy_setopt(c, CURLOPT_POST, 1L);
        api.easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
        api.easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size());
    } else if (m != "GET") {
        api.easy_setopt(c, CURLOPT_CUSTOMREQUEST, m.c_str());
        if (!body.empty()) {
            api.easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
            api.easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size());
        }
    }
    api.easy_setopt(c, CURLOPT_WRITEFUNCTION, &OnData);
    api.easy_setopt(c, CURLOPT_WRITEDATA, &r.body);

    CURLcode rc = api.easy_perform(c);
    long status = 0;
    api.easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (rc == CURLE_OK || rc == CURLE_WRITE_ERROR) r.status = (int)status;   // write error = size limit hit
    api.slist_free_all(hl);
    api.easy_cleanup(c);
    return r;
}

#endif

}  // namespace web
