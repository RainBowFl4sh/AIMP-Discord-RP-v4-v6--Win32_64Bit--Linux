#include "jobs.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "config.h"
#include "update.h"
#include "util.h"
#include "web.h"

namespace jobs {
namespace {

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<std::function<void()>> g_queue;
std::thread g_thread;
bool g_stopping = false;

struct Image {
    std::wstring url;
    std::string  bytes;
    bool         ready = false;
};
std::map<std::string, Image> g_images;
std::wstring g_assetsFor;                           // application whose images were requested
std::map<std::wstring, std::wstring> g_appNames;

void Run() {
    // first update check 20 s after AIMP started (tests: 1 s), then hourly
    uint64_t next = util::TickMs() + (util::GetEnv(L"AIMP_DISCORD_RPC_TEST_URL").empty() ? 20000 : 1000);
    uint64_t look = 0;   // next look whether AIMP has installed an opened update (0: nothing to watch)
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            while (!g_stopping && g_queue.empty()) {
                const uint64_t now = util::TickMs(), due = look && look < next ? look : next;
                if (now >= due) break;
                g_cv.wait_for(lk, std::chrono::milliseconds(due - now));
            }
            if (g_stopping) return;
            if (!g_queue.empty()) {
                job = std::move(g_queue.front());
                g_queue.pop_front();
            }
        }
        if (job) {
            job();
        } else if (util::TickMs() >= next) {
            next = util::TickMs() + 3600 * 1000;
            update::Tick();
        }
        if (!look || util::TickMs() >= look) {
            const uint64_t ms = update::WatchInstall();
            look = ms ? util::TickMs() + ms : 0;
        }
    }
}

bool Digits(const std::wstring& s) { return !s.empty() && s.find_first_not_of(L"0123456789") == std::wstring::npos; }

// AIMP always calls Finalize (-> Stop) first; should the library ever be unloaded without it, do not terminate
struct ThreadGuard {
    ~ThreadGuard() {
        if (g_thread.joinable()) g_thread.detach();
    }
} g_guard;

}  // namespace

void Start() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_thread.joinable()) return;
    g_stopping = false;
    g_thread = std::thread(Run);
}

void Stop() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_stopping = true;
        g_queue.clear();
    }
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
    std::lock_guard<std::mutex> lk(g_mu);
    g_images.clear();
    g_assetsFor.clear();
}

void Post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_stopping || !g_thread.joinable()) return;
        g_queue.push_back(std::move(job));
    }
    g_cv.notify_all();
}

void FetchImage(const std::string& key, const std::wstring& url, const std::wstring& cacheName, int maxAgeDays) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Image& img = g_images[key];
        if (img.url == url) return;   // already loaded / on its way
        img = Image();
        img.url = url;
    }
    Post([key, url, cacheName, maxAgeDays] {
        const std::wstring path = cacheName.empty() ? std::wstring() : config::CacheDir() + util::kPathSep + cacheName;
        const int64_t age = path.empty() ? -1 : util::FileAgeSeconds(path);
        std::string bytes;
        if (age >= 0 && age < maxAgeDays * 86400LL) util::ReadFileBytes(path, bytes, 8u << 20);
        if (!util::LooksLikeImage(bytes.data(), bytes.size())) {
            web::Response r = web::Get(url, 8u << 20);
            if (r.status == 200 && util::LooksLikeImage(r.body.data(), r.body.size())) {
                bytes = std::move(r.body);
                if (!path.empty()) util::WriteFileBytes(path, bytes);
            } else if (age >= 0) {
                util::ReadFileBytes(path, bytes, 8u << 20);   // offline: the old copy is better than nothing
            }
            if (!util::LooksLikeImage(bytes.data(), bytes.size())) bytes.clear();
        }
        {
            std::lock_guard<std::mutex> lk(g_mu);
            auto it = g_images.find(key);
            if (it == g_images.end() || it->second.url != url) return;   // page closed / other image wanted now
            it->second.bytes = std::move(bytes);
            it->second.ready = true;
        }
        NotifyUi();
    });
}

void FetchDiscordAssets(const std::wstring& appId) {
    if (!Digits(appId)) return;   // ends up in URLs and file names
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_assetsFor == appId) return;
        g_assetsFor = appId;
    }
    Post([appId] {
        // the images of the application ("aimp" logo, play / pause icon), as Discord shows them
        const std::wstring cache = config::CacheDir() + util::kPathSep + L"assets_" + appId + L".json";
        std::string json;
        int64_t age = util::FileAgeSeconds(cache);
        if (age < 0 || age > 30 * 86400LL) {
            web::Response r = web::Get(L"https://discord.com/api/v9/oauth2/applications/" + appId + L"/assets");
            if (r.status == 200 && r.body.find('[') != std::string::npos) {
                json = r.body;
                util::WriteFileBytes(cache, json);
            }
        }
        if (json.empty()) util::ReadFileBytes(cache, json, 1u << 20);
        for (const std::string& obj : util::JsonObjects(json, "")) {
            const std::string name = util::JsonGetString(obj, "name");
            const std::wstring id = util::FromUtf8(util::JsonGetString(obj, "id"));
            if (!Digits(id) || (name != "aimp" && name != "play" && name != "pause")) continue;
            const std::wstring n = util::FromUtf8(name);
            FetchImage("asset:" + name, L"https://cdn.discordapp.com/app-assets/" + appId + L"/" + id + L".png?size=160",
                       L"asset_" + appId + L"_" + n + L".png", 30);
        }
        if (appId != kDefaultClientId) {   // own application: its name for "Listening to <name>"
            web::Response r = web::Get(L"https://discord.com/api/v9/applications/" + appId + L"/rpc");
            const std::string name = r.status == 200 ? util::JsonGetString(r.body, "name") : std::string();
            if (!name.empty()) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_appNames[appId] = util::FromUtf8(name);
                }
                NotifyUi();
            }
        }
    });
}

bool TakeImage(const std::string& key, std::string& bytes) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_images.find(key);
    if (it == g_images.end() || !it->second.ready) return false;
    it->second.ready = false;
    bytes = std::move(it->second.bytes);
    return true;
}

std::wstring AppName(const std::wstring& appId) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_appNames.find(appId);
    return it == g_appNames.end() ? std::wstring() : it->second;
}

void ForgetImages() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_images.clear();
    g_assetsFor.clear();
}

}  // namespace jobs
