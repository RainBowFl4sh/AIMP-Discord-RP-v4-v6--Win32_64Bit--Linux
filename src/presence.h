// Background worker: owns the Discord connection, builds the activity JSON and resolves covers.
// The AIMP side only hands over plain-data snapshots, so no AIMP object is ever touched off the main thread.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "config.h"
#include "cover.h"
#include "discord_ipc.h"
#include "track.h"

enum class PlayState { Stopped, Paused, Playing };

struct Snapshot {
    PlayState state    = PlayState::Stopped;
    double    position = 0;   // seconds at 'stamp'
    double    duration = 0;   // seconds, 0 = unknown (radio)
    uint64_t  trackId  = 0;
    TrackInfo track;
    std::chrono::steady_clock::time_point stamp = std::chrono::steady_clock::now();
};

// Why Discord shows nothing for a snapshot
enum class Hidden { No, Disabled, Stopped, Stream, PathFilter, PlaylistFilter, Paused, PausedTimeout };

// The texts of a presence exactly as Discord gets them (also drawn by the live preview on the settings page)
struct ActivityTexts {
    Hidden      hidden  = Hidden::No;
    bool        playing = false;
    double      pos = 0, dur = 0;
    std::string name;                                   // instead of the application name, "" = the app's name
    std::string details, state, largeText, smallText;   // UTF-8, "" = line not shown
    std::string detailsUrl;                             // clickable title, "" = none
};
// rotateStep: which variant of texts with several variants ("%artist% || %title%") is shown
ActivityTexts ComputeTexts(const Config& c, const Snapshot& s, std::chrono::steady_clock::duration pausedFor,
                           int rotateStep = 0);
bool HasVariants(const Config& c);   // a text field contains "||"

struct PresenceStatus {
    enum Kind { Starting, Disabled, NotConnected, Connected } kind = Starting;
    IpcError     error = IpcError::None;
    std::string  errorDetail;           // Discord's own message (English)
    std::string  endpoint;              // pipe / socket of the connection
    DiscordUser  user;
    std::wstring clientId;              // application ID in use
    int64_t      lastSent = 0;          // unix time of the last presence update
    int          connects = 0;          // successful connections so far
    bool         visible  = false;      // a presence is shown in Discord right now
    bool         testing  = false;      // the test presence is shown
    std::string  coverUrl, coverSource; // cover of the current track (see CoverResult)
    bool         coverSearching = false;

    bool operator==(const PresenceStatus& o) const;
};

class PresenceWorker {
public:
    PresenceWorker();
    ~PresenceWorker();

    void Start();
    void Stop();

    void Submit(const Snapshot& s);   // called from the AIMP main thread
    void Refresh();                   // settings changed -> rebuild presence
    void ClearCoverCache();
    void SendTest();                  // show a test presence for 15 seconds
    void Reconnect();                 // drop the connection and connect again at once
    PresenceStatus Status();
    Snapshot LastSnapshot();
    int RotationStep(const Config& c) const;   // current variant step (counted from the start of the track)

private:
    void Run();
    bool BuildActivity(const Config& c, const Snapshot& s, std::string& out);
    std::string TestActivity(const Config& c);
    void SetStatus(const PresenceStatus& st);
    void Wake();
    void WaitForWork(unsigned ms);

    std::thread       thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> resetCover_{false};
    std::atomic<bool> reconnect_{false};
    std::atomic<bool> testRequest_{false};
    std::condition_variable wakeCv_;   // signalled together with mu_
    bool              wake_ = false;

    std::mutex     mu_;
    Snapshot       snap_;
    bool           dirty_ = false;
    PresenceStatus status_;

    // worker-thread only
    DiscordIpc    ipc_;
    CoverResolver cover_;
    PresenceStatus st_;                // working copy of the status
    std::wstring  connectedId_;
    std::chrono::steady_clock::time_point lastConnectTry_{}, lastSend_{}, pausedSince_{}, testUntil_{};
    PlayState     prevState_ = PlayState::Stopped;
    bool          shown_ = false;
    bool          testShown_ = false;
    uint64_t      coverTrackId_ = 0;
    // Discord shows a cover through its media proxy and remembers per URL when that failed ("?" instead of the
    // picture). The proxy copy is checked once after sending; if it fails, the URL is sent again with "?r=N"
    // (a new URL for Discord), then the cover is resolved / uploaded again, at last the AIMP logo is used.
    std::string   coverBust_;             // appended to the cover URL ("?r=1", ...)
    int           coverRepairs_ = 0;      // repairs of this track's cover so far
    bool          awaitShown_ = false;    // waiting for Discord's answer to a SET_ACTIVITY with a cover URL
    std::string   proxyCheck_;            // Discord's proxy URL of the cover, checked at proxyAt_
    std::chrono::steady_clock::time_point proxyAt_{};
    uint64_t      noCoverTrack_ = 0;
    uint64_t      repairTrack_ = 0;       // track the repair counters belong to      // track whose cover Discord cannot show (logo instead)
    void          CheckShownCover(std::chrono::steady_clock::time_point now);
    uint64_t      lastConfigCheck_ = 0;
    unsigned      nextWaitMs_ = 1000;
    std::atomic<uint64_t> rotStartMs_{0};   // TickMs when the current track started (variants start over)
    uint64_t      rotTrack_ = 0;
    int           sentStep_ = -1;           // variant step of the last update sent
};

PresenceWorker& Worker();   // the single instance
