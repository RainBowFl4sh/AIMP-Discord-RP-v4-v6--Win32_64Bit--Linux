#include "web.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#define CURL_DISABLE_TYPECHECK
#include <curl/curl.h>
#include <dlfcn.h>
#endif

#include "util.h"
#include "version.h"

namespace web {

namespace {

std::atomic<bool> g_abort{false};
const char* const kAgent = "AIMP-DiscordRPC/" AIMP_DISCORD_RPC_VERSION " (+https://github.com/" AIMP_DISCORD_RPC_REPO ")";

// Tests: AIMP_DISCORD_RPC_TEST_URL=http://127.0.0.1:port sends every request to a local server
// ("https://api.github.com/x" -> "http://127.0.0.1:port/api.github.com/x").
std::wstring Target(const std::wstring& url) {
    static const std::wstring base = util::GetEnv(L"AIMP_DISCORD_RPC_TEST_URL");
    if (base.empty()) return url;
    size_t p = url.find(L"://");
    return p == std::wstring::npos ? url : base + L"/" + url.substr(p + 3);
}

}  // namespace

#ifdef _WIN32

namespace {

std::mutex g_mu;
std::vector<HINTERNET*> g_open;   // request handles of running requests (closed by Abort)

struct Handle {
    HINTERNET h = nullptr;
    bool tracked = false;
    void Track() {
        std::lock_guard<std::mutex> lk(g_mu);
        g_open.push_back(&h);
        tracked = true;
    }
    ~Handle() {
        std::lock_guard<std::mutex> lk(g_mu);
        if (tracked) g_open.erase(std::remove(g_open.begin(), g_open.end(), &h), g_open.end());
        if (h) WinHttpCloseHandle(h);
    }
};

}  // namespace

void Abort() {
    g_abort = true;
    std::lock_guard<std::mutex> lk(g_mu);
    for (HINTERNET* h : g_open) {   // closing the handle makes the blocked WinHTTP call return at once
        if (*h) WinHttpCloseHandle(*h);
        *h = nullptr;
    }
}

Response Request(const std::wstring& method, const std::wstring& urlIn, const std::vector<std::wstring>& headers,
                 const std::string& body, size_t maxBytes) {
    Response r;
    if (g_abort) return r;
    const std::wstring url = Target(urlIn);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return r;
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring object(uc.lpszUrlPath);   // path + query (rest of the string)
    if (object.empty()) object = L"/";

    Handle session, conn, req;
    session.h = WinHttpOpen(util::FromUtf8(kAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) return r;
    WinHttpSetTimeouts(session.h, 5000, 5000, 10000, 15000);
    conn.h = WinHttpConnect(session.h, host.c_str(), uc.nPort, 0);
    if (!conn.h) return r;
    req.h = WinHttpOpenRequest(conn.h, method.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                               uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) return r;
    req.Track();
    if (method == L"POST") {   // a redirect would turn the upload into an empty GET - report it instead
        DWORD off = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req.h, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof(off));
    }
    if (g_abort) return r;

    std::wstring hdr;
    for (const auto& h : headers) hdr += h + L"\r\n";
    if (!WinHttpSendRequest(req.h, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), hdr.empty() ? 0 : (DWORD)-1L,
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
                            (DWORD)body.size(), 0) ||
        !WinHttpReceiveResponse(req.h, nullptr))
        return r;

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    DWORD hsize = 0;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                        &hsize, WINHTTP_NO_HEADER_INDEX);
    if (hsize > 0 && hsize < 65536) {
        std::wstring h(hsize / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &h[0], &hsize,
                                WINHTTP_NO_HEADER_INDEX))
            r.headers = util::ToUtf8(h.substr(0, hsize / sizeof(wchar_t)));
    }
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) return r;   // aborted / connection lost
        if (avail == 0) break;
        if (r.body.size() + avail > maxBytes) return Response();   // too big
        size_t old = r.body.size();
        r.body.resize(old + avail);
        if (!WinHttpReadData(req.h, &r.body[old], avail, &got)) return Response();
        r.body.resize(old + got);
    }
    r.status = (int)status;
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
            util::Log(L"libcurl not found - online covers and the update check are disabled");
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

struct Sink {
    std::string* body;
    size_t max;
};

size_t OnData(char* p, size_t size, size_t n, void* user) {
    Sink* s = static_cast<Sink*>(user);
    size_t len = size * n;
    if (s->body->size() + len > s->max) return 0;   // too big -> abort the transfer
    s->body->append(p, len);
    return len;
}

size_t OnHeader(char* p, size_t size, size_t n, void* user) {
    std::string* h = static_cast<std::string*>(user);
    if (h->size() < 65536) h->append(p, size * n);
    return size * n;
}

int OnProgress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return g_abort ? 1 : 0; }   // 1 = abort

}  // namespace

void Abort() { g_abort = true; }   // curl checks it in OnProgress (at least once per second)

Response Request(const std::wstring& method, const std::wstring& urlIn, const std::vector<std::wstring>& headers,
                 const std::string& body, size_t maxBytes) {
    Response r;
    const CurlApi& api = Curl();
    if (!api.ok || g_abort) return r;
    CURL* c = api.easy_init();
    if (!c) return r;

    const std::string u = util::ToUtf8(Target(urlIn)), m = util::ToUtf8(method);
    curl_slist* hl = nullptr;
    for (const auto& h : headers) hl = api.slist_append(hl, util::ToUtf8(h).c_str());
    Sink sink = {&r.body, maxBytes};

    api.easy_setopt(c, CURLOPT_URL, u.c_str());
    api.easy_setopt(c, CURLOPT_USERAGENT, kAgent);
    api.easy_setopt(c, CURLOPT_FOLLOWLOCATION, m == "POST" ? 0L : 1L);   // an upload is never turned into a GET
    api.easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    api.easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    api.easy_setopt(c, CURLOPT_TIMEOUT_MS, 30000L);
    api.easy_setopt(c, CURLOPT_NOSIGNAL, 1L);          // we run on a worker thread
    api.easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");    // gzip etc.
    api.easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    api.easy_setopt(c, CURLOPT_XFERINFOFUNCTION, &OnProgress);
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
    api.easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    api.easy_setopt(c, CURLOPT_HEADERFUNCTION, &OnHeader);
    api.easy_setopt(c, CURLOPT_HEADERDATA, &r.headers);

    CURLcode rc = api.easy_perform(c);
    long status = 0;
    api.easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (rc == CURLE_OK) r.status = (int)status;
    else r.body.clear();
    api.slist_free_all(hl);
    api.easy_cleanup(c);
    return r;
}

#endif

void Reset() { g_abort = false; }

}  // namespace web
