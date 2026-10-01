// Background worker: owns the Discord connection, builds the activity JSON and resolves covers.
// The AIMP side only hands over plain-data snapshots, so no AIMP object is ever touched off the main thread.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
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

struct PresenceStatus {
    bool         connected = false;
    std::wstring user;
    std::wstring message;     // human readable state for the settings page
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
    PresenceStatus Status();

private:
    void Run();
    bool BuildActivity(const Config& c, const Snapshot& s, std::string& out);
    void SetStatus(bool connected, const std::wstring& user, const std::wstring& msg);
    void Wake();
    void WaitForWork(unsigned ms);

    std::thread       thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> resetCover_{false};
    std::condition_variable wakeCv_;   // signalled together with mu_
    bool              wake_ = false;

    std::mutex     mu_;
    Snapshot       snap_;
    bool           dirty_ = false;
    PresenceStatus status_;

    // worker-thread only
    DiscordIpc    ipc_;
    CoverResolver cover_;
    std::wstring  connectedId_;
    std::chrono::steady_clock::time_point lastConnectTry_{}, lastSend_{}, pausedSince_{};
    PlayState     prevState_ = PlayState::Stopped;
    bool          shown_ = false;
    uint64_t      coverTrackId_ = 0;
    std::string   coverUrl_;
    uint64_t      lastConfigCheck_ = 0;
};

PresenceWorker& Worker();   // the single instance
