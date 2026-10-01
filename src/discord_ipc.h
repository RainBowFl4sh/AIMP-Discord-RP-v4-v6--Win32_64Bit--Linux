// Minimal Discord Rich Presence client over Discord's local IPC channel:
//   Windows: named pipe \\.\pipe\discord-ipc-N
//   Linux:   Unix socket $XDG_RUNTIME_DIR/discord-ipc-N (also Flatpak / Snap Discord)
//   Wine:    the Windows DLL falls back to the Linux Unix socket when no pipe exists (direct Linux syscalls,
//            the same technique as wine-discord-ipc-bridge - Wine runs the DLL's code natively on Linux)
// Not thread safe: use it from one thread only.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class DiscordIpc {
public:
    ~DiscordIpc() { Disconnect(); }

    bool Connect(const std::wstring& clientId);   // tries discord-ipc-0..9 + handshake
    void Disconnect();
    bool Connected() const;

    void Pump();                                  // read pending frames (PING, errors, close)
    bool SetActivity(const std::string& activityJson);  // empty string = clear presence

    const std::string& UserName() const  { return user_; }
    const std::string& LastError() const { return lastError_; }

private:
    int  ReadFrame(uint32_t& op, std::string& payload, uint32_t timeoutMs);  // 1 ok, 0 nothing, -1 error
    bool WriteFrame(uint32_t op, const std::string& payload);
    void HandleFrame(uint32_t op, const std::string& payload);

    // transport
    bool OpenChannel();
    bool IoWrite(const void* data, size_t len);
    bool IoAvailable(size_t& avail);              // false = connection lost
    bool IoRead(void* data, size_t len, size_t& got);

#ifdef _WIN32
    bool  OpenUnixSocketUnderWine();
    void* pipe_   = nullptr;   // HANDLE of the named pipe (nullptr = none)
    long  wineFd_ = -1;        // Linux socket fd when running in Wine
#else
    int    fd_ = -1;
#endif
    std::string user_;
    std::string lastError_;
    uint32_t    nonce_ = 0;
};

// Folders that may contain Discord's Unix socket on Linux, most likely first (native, Flatpak, Snap, /tmp).
std::vector<std::string> DiscordSocketDirs(const std::string& xdgRuntimeDir, const std::string& tmpDir);
