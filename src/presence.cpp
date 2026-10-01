#include "presence.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <vector>

#include "util.h"

namespace {

using Vars = std::map<std::wstring, std::wstring>;
using clock_t_ = std::chrono::steady_clock;

// Replaces %name% placeholders. Unknown placeholders stay untouched. With urlEncode every value is percent-encoded.
std::wstring Expand(const std::wstring& tmpl, const Vars& vars, bool urlEncode) {
    std::wstring out;
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == L'%') {
            size_t j = tmpl.find(L'%', i + 1);
            if (j != std::wstring::npos) {
                auto it = vars.find(util::Lower(tmpl.substr(i + 1, j - i - 1)));
                if (it != vars.end()) {
                    out += urlEncode ? util::FromUtf8(util::UrlEncode(util::ToUtf8(it->second))) : it->second;
                    i = j + 1;
                    continue;
                }
            }
        }
        out += tmpl[i++];
    }
    return out;
}

std::wstring MakeBar(double fraction, int length) {
    fraction = std::min(1.0, std::max(0.0, fraction));
    int filled = (int)std::floor(fraction * length + 0.5);
    std::wstring bar;
    for (int i = 0; i < length; ++i) bar += (i < filled) ? L"\u25B0" : L"\u25B1";
    return bar;
}

// Discord wants 2..128 bytes for text fields.
std::string Field(const std::wstring& w) {
    std::string u = util::TruncateUtf8(util::ToUtf8(util::Trim(w)), 128);
    if (u.empty()) return u;
    size_t chars = 0;
    for (unsigned char c : u) if ((c & 0xC0) != 0x80) ++chars;
    if (chars < 2) u += "\xC2\xA0";  // pad with a no-break space
    return u;
}

bool IsHttpUrl(const std::wstring& u) {
    std::wstring l = util::Lower(u.substr(0, 8));
    return l.rfind(L"http://", 0) == 0 || l.rfind(L"https://", 0) == 0;
}

bool NeedsPeriodicRefresh(const Config& c) {
    for (const std::wstring* s : {&c.details, &c.state, &c.largeText, &c.smallText}) {
        std::wstring l = util::Lower(*s);
        if (l.find(L"%pos%") != std::wstring::npos || l.find(L"%bar%") != std::wstring::npos ||
            l.find(L"%percent%") != std::wstring::npos)
            return true;
    }
    return false;
}

}  // namespace

PresenceWorker& Worker() {
    static PresenceWorker w;
    return w;
}

PresenceWorker::PresenceWorker() { wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); }

PresenceWorker::~PresenceWorker() {
    Stop();
    if (wake_) CloseHandle(wake_);
}

void PresenceWorker::Start() {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread(&PresenceWorker::Run, this);
}

void PresenceWorker::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    SetEvent(wake_);
    thread_.join();
}

void PresenceWorker::Submit(const Snapshot& s) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        snap_ = s;
        dirty_ = true;
    }
    SetEvent(wake_);
}

void PresenceWorker::Refresh() {
    resetCover_ = true;
    {
        std::lock_guard<std::mutex> lk(mu_);
        dirty_ = true;
    }
    SetEvent(wake_);
}

void PresenceWorker::ClearCoverCache() {
    cover_.ClearCache();   // internally synchronized
    Refresh();
}

PresenceStatus PresenceWorker::Status() {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

void PresenceWorker::SetStatus(bool connected, const std::wstring& user, const std::wstring& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    status_.connected = connected;
    status_.user = user;
    status_.message = msg;
}

// ---------------------------------------------------------------------------------------------------------------

bool PresenceWorker::BuildActivity(const Config& c, const Snapshot& s, std::string& out) {
    if (s.state == PlayState::Stopped) return false;
    if (s.track.fileName.empty() && s.track.title.empty()) return false;
    if (c.hideStreams && util::IsUrl(s.track.fileName)) return false;
    for (const auto& raw : util::Split(c.excludePaths, L';')) {
        std::wstring ex = util::Trim(raw);
        if (!ex.empty() && util::ContainsNoCase(s.track.fileName, ex)) return false;
    }

    const auto now = clock_t_::now();
    if (s.state == PlayState::Paused) {
        if (c.pausedBehavior == 1) return false;
        if (c.clearAfterPaused > 0 && now - pausedSince_ >= std::chrono::minutes(c.clearAfterPaused)) return false;
    }

    const bool playing = (s.state == PlayState::Playing);
    double pos = s.position;
    if (playing) pos += std::chrono::duration<double>(now - s.stamp).count();
    if (s.duration > 0 && pos > s.duration) pos = s.duration;
    if (pos < 0) pos = 0;

    Vars v;
    v[L"artist"]      = s.track.artist;
    v[L"albumartist"] = s.track.albumArtist.empty() ? s.track.artist : s.track.albumArtist;
    v[L"title"]       = s.track.title;
    v[L"album"]       = s.track.album;
    v[L"genre"]       = s.track.genre;
    v[L"year"]        = s.track.year;
    v[L"track"]       = s.track.trackNumber;
    v[L"filename"]    = util::FileNameNoExt(s.track.fileName);
    v[L"ext"]         = util::FileExt(s.track.fileName);
    v[L"pos"]         = util::FormatTime(pos);
    v[L"dur"]         = s.duration > 0 ? util::FormatTime(s.duration) : L"--:--";
    v[L"percent"]     = s.duration > 0 ? std::to_wstring((int)(pos / s.duration * 100.0)) : L"0";
    v[L"bar"]         = MakeBar(s.duration > 0 ? pos / s.duration : 0.0, c.barLength);
    v[L"status"]      = playing ? L"Playing" : L"Paused";

    std::string details = Field(Expand(c.details, v, false));
    std::string state   = Field(Expand(c.state, v, false));
    if (details.empty() && state.empty()) details = Field(v[L"filename"]);

    std::string a = "{\"type\":" + std::to_string(c.activityType) + ",\"status_display_type\":" +
                    std::to_string(c.statusDisplay);
    if (!details.empty()) a += ",\"details\":\"" + util::JsonEscape(details) + "\"";
    if (!state.empty())   a += ",\"state\":\"" + util::JsonEscape(state) + "\"";

    // clickable title / artist line (Discord: details_url / state_url, max. 256 chars)
    auto addLink = [&](const char* key, const std::string& line, const std::wstring& urlT) {
        if (line.empty() || urlT.empty()) return;
        std::wstring url = util::Trim(Expand(urlT, v, true));
        if (url.size() > 256) {  // very long names: fall back to searching the title only
            Vars shortV = v;
            shortV[L"artist"] = L"";
            url = util::Trim(Expand(urlT, shortV, true));
        }
        if (!IsHttpUrl(url) || url.size() > 256) return;
        a += std::string(",\"") + key + "\":\"" + util::JsonEscape(util::ToUtf8(url)) + "\"";
    };
    addLink("details_url", details, c.detailsUrl);
    addLink("state_url", state, c.stateUrl);

    // progress bar (Listening/Watching) or elapsed counter (Playing)
    if (c.showTimestamps && playing) {
        long long nowUnix = (long long)time(nullptr);
        long long start = nowUnix - (long long)pos;
        a += ",\"timestamps\":{\"start\":" + std::to_string(start);
        if (s.duration > 0) a += ",\"end\":" + std::to_string(start + (long long)(s.duration + 0.5));
        a += "}";
    }

    // images
    std::string large = (c.coverEnabled && !coverUrl_.empty()) ? coverUrl_ : util::ToUtf8(c.fallbackKey);
    std::string smallImg = c.showSmallIcon ? util::ToUtf8(playing ? c.playKey : c.pauseKey) : std::string();
    std::string largeText = Field(Expand(c.largeText, v, false));
    std::string smallText = Field(Expand(c.smallText, v, false));
    if (!large.empty() || !smallImg.empty()) {
        std::string as;
        auto add = [&](const char* key, const std::string& val) {
            if (val.empty()) return;
            if (!as.empty()) as += ",";
            as += std::string("\"") + key + "\":\"" + util::JsonEscape(val) + "\"";
        };
        if (!large.empty()) { add("large_image", large); add("large_text", largeText); }
        if (!smallImg.empty()) { add("small_image", smallImg); add("small_text", smallText); }
        a += ",\"assets\":{" + as + "}";
    }

    // buttons (Discord shows max. 2, and never on your own client)
    std::string buttons;
    auto addButton = [&](bool enabled, const std::wstring& labelT, const std::wstring& urlT) {
        if (!enabled) return;
        std::wstring label = util::Trim(Expand(labelT, v, false));
        std::wstring url = util::Trim(Expand(urlT, v, true));
        if (label.empty() || !IsHttpUrl(url) || url.size() > 512) return;
        if (label.size() > 32) label = label.substr(0, 31) + L"\u2026";
        if (!buttons.empty()) buttons += ",";
        buttons += "{\"label\":\"" + util::JsonEscape(util::ToUtf8(label)) + "\",\"url\":\"" +
                   util::JsonEscape(util::ToUtf8(url)) + "\"}";
    };
    addButton(c.btn1Enabled, c.btn1Label, c.btn1Url);
    addButton(c.btn2Enabled, c.btn2Label, c.btn2Url);
    if (!buttons.empty()) a += ",\"buttons\":[" + buttons + "]";

    a += "}";
    out = std::move(a);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------

void PresenceWorker::Run() {
    using namespace std::chrono;

    while (!stop_) {
        WaitForSingleObject(wake_, 250);
        if (stop_) break;

        Config cfg = config::Get();
        if (resetCover_.exchange(false)) coverTrackId_ = 0;

        // ---- disabled / not configured
        const bool active = cfg.enabled && !cfg.clientId.empty();
        if (!active) {
            if (ipc_.Connected()) {
                ipc_.SetActivity("");
                Sleep(50);
                ipc_.Disconnect();
            }
            shown_ = false;
            SetStatus(false, L"", cfg.enabled ? L"Enter your Discord Application ID on the General tab" : L"Disabled");
            continue;
        }

        // ---- (re)connect
        if (ipc_.Connected() && connectedId_ != cfg.clientId) ipc_.Disconnect();
        auto now = steady_clock::now();
        bool justConnected = false;
        if (!ipc_.Connected()) {
            shown_ = false;
            if (now - lastConnectTry_ >= seconds(5)) {
                lastConnectTry_ = now;
                if (ipc_.Connect(cfg.clientId)) {
                    connectedId_ = cfg.clientId;
                    justConnected = true;
                } else {
                    SetStatus(false, L"", L"Not connected: " + util::FromUtf8(ipc_.LastError()) + L" (retrying every 5 s)");
                }
            }
            if (!ipc_.Connected()) continue;
        }
        ipc_.Pump();
        if (!ipc_.Connected()) {
            lastConnectTry_ = now;
            SetStatus(false, L"", L"Disconnected: " + util::FromUtf8(ipc_.LastError()));
            continue;
        }
        SetStatus(true, util::FromUtf8(ipc_.UserName()), L"Connected");

        // ---- decide whether to (re)send
        Snapshot snap;
        bool want = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snap = snap_;
            want = dirty_;
        }
        if (justConnected) want = true;
        if (snap.state != prevState_) {
            if (snap.state == PlayState::Paused) pausedSince_ = now;
            prevState_ = snap.state;
        }
        if (!want) {
            if (snap.state == PlayState::Playing && NeedsPeriodicRefresh(cfg) &&
                now - lastSend_ >= seconds(cfg.refreshSeconds))
                want = true;
            else if (snap.state == PlayState::Paused && shown_ && cfg.clearAfterPaused > 0 &&
                     now - pausedSince_ >= minutes(cfg.clearAfterPaused))
                want = true;
        }
        if (!want) continue;
        if (!justConnected && now - lastSend_ < seconds(2)) continue;  // Discord rate limit: coalesce updates

        {
            std::lock_guard<std::mutex> lk(mu_);
            dirty_ = false;
            snap = snap_;  // newest data
        }

        // ---- cover: use cache immediately, resolve (network) after the first update went out
        bool needResolve = false;
        if (!cfg.coverEnabled) {
            coverUrl_.clear();
        } else if (snap.state != PlayState::Stopped && snap.trackId != coverTrackId_) {
            coverTrackId_ = snap.trackId;
            coverUrl_ = cover_.PeekCache(snap.track);
            needResolve = coverUrl_.empty();
        }

        std::string activity;
        bool show = BuildActivity(cfg, snap, activity);
        if (show) {
            ipc_.SetActivity(activity);
            shown_ = true;
        } else if (shown_) {
            ipc_.SetActivity("");
            shown_ = false;
        }
        lastSend_ = steady_clock::now();

        if (show && needResolve) {
            std::string url = cover_.Resolve(snap.track, cfg);
            if (!url.empty() && coverTrackId_ == snap.trackId) {
                coverUrl_ = url;
                std::lock_guard<std::mutex> lk(mu_);
                if (snap_.trackId == snap.trackId) dirty_ = true;   // resend with the cover
            }
        }
    }

    // leaving: remove the presence so it does not linger after AIMP closed
    if (ipc_.Connected()) {
        ipc_.SetActivity("");
        Sleep(100);
        ipc_.Disconnect();
    }
}
