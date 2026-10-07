#include "presence.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <vector>

#include "i18n.h"
#include "util.h"
#include "version.h"

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
    for (int i = 0; i < length; ++i) bar += (i < filled) ? L"▰" : L"▱";
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
    for (const std::wstring* s : {&c.activityName, &c.details, &c.state, &c.largeText, &c.smallText}) {
        std::wstring l = util::Lower(*s);
        if (l.find(L"%pos%") != std::wstring::npos || l.find(L"%bar%") != std::wstring::npos ||
            l.find(L"%percent%") != std::wstring::npos)
            return true;
    }
    return false;
}

std::string Quote(const std::string& s) { return "\"" + util::JsonEscape(s) + "\""; }

// "%artist% || %title%": the variant for this step (texts without "||" stay as they are)
std::wstring Variant(const std::wstring& tmpl, int step) {
    if (tmpl.find(L"||") == std::wstring::npos) return tmpl;
    std::vector<std::wstring> parts;
    for (size_t from = 0;;) {
        const size_t at = tmpl.find(L"||", from);
        parts.push_back(util::Trim(tmpl.substr(from, at == std::wstring::npos ? std::wstring::npos : at - from)));
        if (at == std::wstring::npos) break;
        from = at + 2;
    }
    return parts[(size_t)(step < 0 ? 0 : step) % parts.size()];
}

}  // namespace

bool HasVariants(const Config& c) {
    for (const std::wstring* s : {&c.activityName, &c.details, &c.state, &c.largeText, &c.smallText})
        if (s->find(L"||") != std::wstring::npos) return true;
    return false;
}

bool PresenceStatus::operator==(const PresenceStatus& o) const {
    return kind == o.kind && error == o.error && errorDetail == o.errorDetail && endpoint == o.endpoint &&
           user.id == o.user.id && user.name == o.user.name && user.avatar == o.user.avatar &&
           user.globalName == o.user.globalName && clientId == o.clientId && lastSent == o.lastSent && connects == o.connects &&
           visible == o.visible && testing == o.testing && coverUrl == o.coverUrl && coverSource == o.coverSource &&
           coverSearching == o.coverSearching;
}

ActivityTexts ComputeTexts(const Config& c, const Snapshot& s, clock_t_::duration pausedFor, int rotateStep) {
    ActivityTexts t;
    const bool playing = s.state == PlayState::Playing;
    if (!c.enabled)                                                     t.hidden = Hidden::Disabled;
    else if (s.state == PlayState::Stopped || (s.track.fileName.empty() && s.track.title.empty())) t.hidden = Hidden::Stopped;
    else if (c.hideStreams && util::IsUrl(s.track.fileName))            t.hidden = Hidden::Stream;
    else if (util::MatchesAny(s.track.fileName, c.excludePaths))        t.hidden = Hidden::PathFilter;
    else if (util::MatchesAny(s.track.playlist, c.excludePlaylists))    t.hidden = Hidden::PlaylistFilter;
    else if (!playing && c.pausedBehavior == 1)                         t.hidden = Hidden::Paused;
    else if (!playing && c.clearAfterPaused > 0 && pausedFor >= std::chrono::minutes(c.clearAfterPaused))
        t.hidden = Hidden::PausedTimeout;

    double pos = s.position;
    if (playing) pos += std::chrono::duration<double>(clock_t_::now() - s.stamp).count();
    if (s.duration > 0 && pos > s.duration) pos = s.duration;
    if (pos < 0) pos = 0;
    t.playing = playing;
    t.pos = pos;
    t.dur = s.duration;

    Vars v;
    v[L"artist"]      = s.track.artist;
    v[L"albumartist"] = s.track.albumArtist.empty() ? s.track.artist : s.track.albumArtist;
    v[L"title"]       = s.track.title;
    v[L"album"]       = s.track.album;
    v[L"genre"]       = s.track.genre;
    v[L"year"]        = s.track.year;
    v[L"track"]       = s.track.trackNumber;
    v[L"playlist"]    = s.track.playlist;
    v[L"filename"]    = util::FileNameNoExt(s.track.fileName);
    v[L"ext"]         = util::FileExt(s.track.fileName);
    v[L"pos"]         = util::FormatTime(pos);
    v[L"dur"]         = s.duration > 0 ? util::FormatTime(s.duration) : L"--:--";
    v[L"percent"]     = s.duration > 0 ? std::to_wstring((int)(pos / s.duration * 100.0)) : L"0";
    v[L"bar"]         = MakeBar(s.duration > 0 ? pos / s.duration : 0.0, c.barLength);
    v[L"status"]      = i18n::T(playing ? "Status.Playing" : "Status.Paused");

    t.name      = Field(Expand(Variant(c.activityName, rotateStep), v, false));
    t.details   = Field(Expand(Variant(c.details, rotateStep), v, false));
    t.state     = Field(Expand(Variant(c.state, rotateStep), v, false));
    t.largeText = Field(Expand(Variant(c.largeText, rotateStep), v, false));
    t.smallText = c.showSmallIcon ? Field(Expand(Variant(c.smallText, rotateStep), v, false)) : std::string();
    if (t.details.empty() && t.state.empty()) t.details = Field(v[L"filename"]);

    // clickable title (Discord: details_url, max. 256 chars)
    if (!t.details.empty() && !c.detailsUrl.empty()) {
        std::wstring url = util::Trim(Expand(c.detailsUrl, v, true));
        if (url.size() > 256) {   // very long names: fall back to searching the title only
            Vars shortV = v;
            shortV[L"artist"] = L"";
            url = util::Trim(Expand(c.detailsUrl, shortV, true));
        }
        if (IsHttpUrl(url) && url.size() <= 256) t.detailsUrl = util::ToUtf8(url);
    }
    return t;
}

PresenceWorker& Worker() {
    static PresenceWorker w;
    return w;
}

PresenceWorker::PresenceWorker() = default;

PresenceWorker::~PresenceWorker() { Stop(); }

void PresenceWorker::Wake() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        wake_ = true;
    }
    wakeCv_.notify_one();
}

void PresenceWorker::WaitForWork(unsigned ms) {
    std::unique_lock<std::mutex> lk(mu_);
    wakeCv_.wait_for(lk, std::chrono::milliseconds(ms), [this] { return wake_ || stop_.load(); });
    wake_ = false;
}

void PresenceWorker::Start() {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread(&PresenceWorker::Run, this);
}

void PresenceWorker::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    Wake();
    thread_.join();
}

void PresenceWorker::Submit(const Snapshot& s) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        snap_ = s;
        dirty_ = true;
    }
    Wake();
}

void PresenceWorker::Refresh() {
    resetCover_ = true;
    {
        std::lock_guard<std::mutex> lk(mu_);
        dirty_ = true;
    }
    Wake();
}

void PresenceWorker::ClearCoverCache() {
    cover_.ClearCache();   // internally synchronized
    Refresh();
}

void PresenceWorker::SendTest() {
    testRequest_ = true;
    Wake();
}

void PresenceWorker::Reconnect() {
    reconnect_ = true;
    Wake();
}

PresenceStatus PresenceWorker::Status() {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

int PresenceWorker::RotationStep(const Config& c) const {
    if (!HasVariants(c)) return 0;
    const uint64_t start = rotStartMs_.load(), now = util::TickMs();
    return now > start ? (int)((now - start) / ((uint64_t)std::max(5, c.rotateSeconds) * 1000)) : 0;
}

Snapshot PresenceWorker::LastSnapshot() {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}

void PresenceWorker::SetStatus(const PresenceStatus& st) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (status_ == st) return;
        status_ = st;
    }
    NotifyUi();   // settings page (if open) shows the new state
}

// ---------------------------------------------------------------------------------------------------------------

void PresenceWorker::CheckShownCover(std::chrono::steady_clock::time_point now) {
    using namespace std::chrono;
    const std::string shown = ipc_.TakeShownImage();
    if (awaitShown_ && !shown.empty()) {
        awaitShown_ = false;
        if (shown.rfind("mp:external/", 0) == 0) {   // Discord's copy: checked in a few seconds
            proxyCheck_ = "https://media.discordapp.net/external/" + shown.substr(12);
            proxyAt_ = now + seconds(4);
        }
    }
    if (proxyCheck_.empty() || now < proxyAt_) {
        if (!proxyCheck_.empty()) nextWaitMs_ = std::min<unsigned>(nextWaitMs_, 1000);
        return;
    }
    std::string url;
    url.swap(proxyCheck_);
    if (CoverResolver::Reachable(url)) return;   // Discord can show it

    util::Log(L"Discord cannot load the cover (%ls) - repairing", util::FromUtf8(st_.coverUrl + coverBust_).c_str());
    ++coverRepairs_;
    if (coverRepairs_ <= 2) {   // the same picture under a new URL: Discord loads it again
        coverBust_ = std::string(st_.coverUrl.find('?') == std::string::npos ? "?" : "&") + "r=" +
                     std::to_string(coverRepairs_);
    } else if (coverRepairs_ == 3) {   // resolve / upload it again
        Snapshot snap;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snap = snap_;
        }
        if (snap.trackId == coverTrackId_) cover_.Forget(snap.track);
        coverTrackId_ = 0;   // -> resolved again with the next update (repairTrack_ keeps the count)
        coverBust_.clear();
    } else {   // give up for this track: the AIMP logo instead of a "?"
        util::Log(L"Discord cannot show this cover - showing the AIMP logo for this track");
        noCoverTrack_ = coverTrackId_;
    }
    std::lock_guard<std::mutex> lk(mu_);
    dirty_ = true;
}

bool PresenceWorker::BuildActivity(const Config& c, const Snapshot& s, std::string& out) {
    const ActivityTexts t = ComputeTexts(c, s, clock_t_::now() - pausedSince_, RotationStep(c));
    if (t.hidden != Hidden::No) return false;

    std::string a = "{\"type\":" + std::to_string(c.activityType) + ",\"status_display_type\":" +
                    std::to_string(c.statusDisplay);
    if (!t.name.empty()) a += ",\"name\":" + Quote(t.name);   // Discord shows it instead of the application's name
    if (!t.details.empty()) a += ",\"details\":" + Quote(t.details);
    if (!t.state.empty()) a += ",\"state\":" + Quote(t.state);
    if (!t.detailsUrl.empty()) a += ",\"details_url\":" + Quote(t.detailsUrl);

    // progress bar (Listening) or elapsed counter (Playing)
    if (c.showTimestamps && t.playing) {
        long long start = (long long)time(nullptr) - (long long)t.pos;
        a += ",\"timestamps\":{\"start\":" + std::to_string(start);
        if (t.dur > 0) a += ",\"end\":" + std::to_string(start + (long long)(t.dur + 0.5));
        a += "}";
    }

    // images: cover (or the application's "aimp" image) + play / pause icon
    const bool cover = c.coverEnabled && !st_.coverUrl.empty() && noCoverTrack_ != coverTrackId_;
    a += ",\"assets\":{\"large_image\":" + Quote(cover ? st_.coverUrl + coverBust_ : "aimp");
    if (!t.largeText.empty()) a += ",\"large_text\":" + Quote(t.largeText);
    if (c.showSmallIcon) {
        a += ",\"small_image\":" + Quote(t.playing ? "play" : "pause");
        if (!t.smallText.empty()) a += ",\"small_text\":" + Quote(t.smallText);
    }
    a += "}}";
    out = std::move(a);
    return true;
}

std::string PresenceWorker::TestActivity(const Config& c) {
    return "{\"type\":" + std::to_string(c.activityType) + ",\"details\":" +
           Quote(util::ToUtf8(i18n::T("Test.Details"))) + ",\"state\":" + Quote(util::ToUtf8(i18n::T("Test.State"))) +
           ",\"timestamps\":{\"start\":" + std::to_string((long long)time(nullptr)) +
           "},\"assets\":{\"large_image\":\"aimp\",\"large_text\":\"AIMP Discord Rich Presence " AIMP_DISCORD_RPC_VERSION
           "\",\"small_image\":\"play\",\"small_text\":" + Quote(util::ToUtf8(i18n::T("Test.Small"))) + "}}";
}

// ---------------------------------------------------------------------------------------------------------------

void PresenceWorker::Run() {
    using namespace std::chrono;

    while (!stop_) {
        WaitForWork(nextWaitMs_);   // ~1 s while idle: Discord connection check, INI file check
        nextWaitMs_ = 1000;
        if (stop_) break;

        // the INI file may be edited by hand while AIMP runs (checked about every 2 s)
        if (util::TickMs() - lastConfigCheck_ >= 2000) {
            lastConfigCheck_ = util::TickMs();
            if (config::ReloadIfChanged()) {
                resetCover_ = true;
                std::lock_guard<std::mutex> lk(mu_);
                dirty_ = true;
            }
        }

        Config cfg = config::Get();
        if (resetCover_.exchange(false)) coverTrackId_ = 0;
        auto now = steady_clock::now();
        if (testRequest_.exchange(false)) {
            testUntil_ = now + seconds(15);
            testShown_ = false;
        }
        const bool testing = now < testUntil_;
        st_.clientId = cfg.clientId;
        st_.testing = testing;

        // ---- disabled
        if (!cfg.enabled && !testing) {
            if (ipc_.Connected()) {
                ipc_.SetActivity("");
                util::SleepMs(50);
                ipc_.Disconnect();
            }
            shown_ = false;
            st_.kind = PresenceStatus::Disabled;
            st_.visible = false;
            SetStatus(st_);
            continue;
        }

        // ---- (re)connect
        if (reconnect_.exchange(false)) {
            if (ipc_.Connected()) {
                ipc_.SetActivity("");
                ipc_.Disconnect();
            }
            lastConnectTry_ = {};
            util::Log(L"Reconnecting (requested on the settings page)");
        }
        if (ipc_.Connected() && connectedId_ != cfg.clientId) ipc_.Disconnect();
        bool justConnected = false;
        if (!ipc_.Connected()) {
            shown_ = false;
            st_.visible = false;
            if (now - lastConnectTry_ >= seconds(5)) {
                lastConnectTry_ = now;
                if (ipc_.Connect(cfg.clientId)) {
                    connectedId_ = cfg.clientId;
                    justConnected = true;
                    ++st_.connects;
                    util::Log(L"Connected to Discord (%ls)", util::FromUtf8(ipc_.Endpoint()).c_str());
                } else {
                    st_.kind = PresenceStatus::NotConnected;
                    st_.error = ipc_.Error();
                    st_.errorDetail = ipc_.ErrorDetail();
                    SetStatus(st_);
                }
            }
            if (!ipc_.Connected()) continue;
        }
        ipc_.Pump();
        if (!ipc_.Connected()) {
            lastConnectTry_ = now;
            st_.kind = PresenceStatus::NotConnected;
            st_.error = ipc_.Error() == IpcError::None ? IpcError::Closed : ipc_.Error();
            st_.errorDetail = ipc_.ErrorDetail();
            st_.visible = false;
            SetStatus(st_);
            util::Log(L"Disconnected from Discord");
            continue;
        }
        CheckShownCover(now);
        st_.kind = PresenceStatus::Connected;
        st_.error = IpcError::None;
        st_.errorDetail.clear();
        st_.endpoint = ipc_.Endpoint();
        st_.user = ipc_.User();
        SetStatus(st_);

        // ---- test presence (Advanced tab)
        if (testing) {
            if (!testShown_ && now - lastSend_ >= seconds(2)) {
                ipc_.SetActivity(TestActivity(cfg));
                testShown_ = shown_ = true;
                lastSend_ = now;
                st_.lastSent = util::UnixTime();
                st_.visible = true;
                SetStatus(st_);
                util::Log(L"Test presence sent");
            }
            continue;
        }
        if (testShown_) {   // test over: back to the normal presence
            testShown_ = false;
            std::lock_guard<std::mutex> lk(mu_);
            dirty_ = true;
        }

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
        if (snap.trackId != rotTrack_) {   // new track: texts with variants start with the first one
            rotTrack_ = snap.trackId;
            rotStartMs_ = util::TickMs();
        }
        if (!want) {
            if (snap.state == PlayState::Playing && NeedsPeriodicRefresh(cfg) &&
                now - lastSend_ >= seconds(cfg.refreshSeconds))
                want = true;
            else if (snap.state == PlayState::Paused && shown_ && cfg.clearAfterPaused > 0 &&
                     now - pausedSince_ >= minutes(cfg.clearAfterPaused))
                want = true;
            else if (shown_ && snap.state != PlayState::Stopped && HasVariants(cfg) && RotationStep(cfg) != sentStep_)
                want = true;   // the next variant of the texts is due
        }
        if (!want) continue;
        if (!justConnected && now - lastSend_ < seconds(2)) {   // Discord rate limit: coalesce updates
            nextWaitMs_ = (unsigned)duration_cast<milliseconds>(lastSend_ + seconds(2) - now).count() + 10;
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(mu_);
            dirty_ = false;
            snap = snap_;   // newest data
        }
        cfg = config::Get();   // newest settings as well: they may have been saved while (re)connecting

        // ---- cover: use the cache immediately, resolve (network) after the first update went out
        bool needResolve = false;
        if (!cfg.coverEnabled || snap.state == PlayState::Stopped) {
            st_.coverUrl.clear();
            st_.coverSource.clear();
            coverTrackId_ = 0;
        } else if (snap.trackId != coverTrackId_) {
            coverTrackId_ = snap.trackId;
            proxyCheck_.clear();
            if (repairTrack_ != snap.trackId) {   // another track: its cover starts without repairs
                repairTrack_ = snap.trackId;
                coverBust_.clear();
                coverRepairs_ = 0;
            }
            CoverResult r = cover_.PeekCache(snap.track);
            st_.coverUrl = r.url;
            st_.coverSource = r.source;
            needResolve = r.url.empty();
        }

        std::string activity;
        bool show = BuildActivity(cfg, snap, activity);
        if (show) {
            ipc_.SetActivity(activity);
            shown_ = true;
            ipc_.TakeShownImage();   // (an older answer)
            awaitShown_ = cfg.coverEnabled && st_.coverUrl.rfind("http", 0) == 0 && noCoverTrack_ != coverTrackId_;
        } else if (shown_) {
            ipc_.SetActivity("");
            shown_ = false;
        }
        lastSend_ = steady_clock::now();
        sentStep_ = RotationStep(cfg);
        st_.lastSent = util::UnixTime();
        st_.visible = shown_;
        st_.coverSearching = show && needResolve;
        SetStatus(st_);

        if (show && needResolve) {
            CoverResult r = cover_.Resolve(snap.track, cfg);
            st_.coverSearching = false;
            if (coverTrackId_ == snap.trackId) {
                st_.coverUrl = r.url;
                st_.coverSource = r.source;
                if (!r.url.empty()) {
                    std::lock_guard<std::mutex> lk(mu_);
                    if (snap_.trackId == snap.trackId) dirty_ = true;   // resend with the cover
                }
            }
            SetStatus(st_);
        }
    }

    // leaving: remove the presence so it does not linger after AIMP closed
    if (ipc_.Connected()) {
        ipc_.SetActivity("");
        util::SleepMs(100);
        ipc_.Disconnect();
    }
}
