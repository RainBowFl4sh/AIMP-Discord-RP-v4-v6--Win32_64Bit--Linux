// Tiny blocking HTTP(S) client (Windows: WinHTTP, Linux: libcurl loaded at run time).
#pragma once
#include <functional>
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

// For the calling thread only: while 'stop' returns true, new requests fail at once (status 0) and a running one is
// ended (Linux: within a moment; Windows: it is not started any more). Empty function = never. Restores on scope exit.
class CancelScope {
public:
    explicit CancelScope(std::function<bool()> stop);
    ~CancelScope();
    CancelScope(const CancelScope&) = delete;
    CancelScope& operator=(const CancelScope&) = delete;
private:
    std::function<bool()> stop_;
    const std::function<bool()>* previous_;
};
bool Cancelled();      // the calling thread's stop function says so
void CheckCancels();   // Windows: ends blocked requests whose CancelScope says stop (call it now and then, any thread)
void Reset();   // plugin (re)initialized

}  // namespace web
