// Minimal Discord Rich Presence client over the local named pipe (\\.\pipe\discord-ipc-N).
// Not thread safe: use it from one thread only.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

class DiscordIpc {
public:
    ~DiscordIpc() { Disconnect(); }

    bool Connect(const std::wstring& clientId);   // tries pipes 0..9 + handshake
    void Disconnect();
    bool Connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    void Pump();                                  // read pending frames (PING, errors, close)
    bool SetActivity(const std::string& activityJson);  // empty string = clear presence

    const std::string& UserName() const  { return user_; }
    const std::string& LastError() const { return lastError_; }

private:
    int  ReadFrame(uint32_t& op, std::string& payload, DWORD timeoutMs);  // 1 ok, 0 nothing, -1 error
    bool WriteFrame(uint32_t op, const std::string& payload);
    void HandleFrame(uint32_t op, const std::string& payload);

    HANDLE      pipe_ = INVALID_HANDLE_VALUE;
    std::string user_;
    std::string lastError_;
    uint32_t    nonce_ = 0;
};
