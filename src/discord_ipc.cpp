#include "discord_ipc.h"

#include <cstring>

#include "util.h"

namespace {
enum : uint32_t { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };
}

bool DiscordIpc::Connect(const std::wstring& clientId) {
    Disconnect();
    user_.clear();
    lastError_.clear();

    for (int i = 0; i < 10 && pipe_ == INVALID_HANDLE_VALUE; ++i) {
        std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeW(name.c_str(), 300)) {
            h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (h != INVALID_HANDLE_VALUE) pipe_ = h;
    }
    if (pipe_ == INVALID_HANDLE_VALUE) {
        lastError_ = "Discord is not running";
        return false;
    }

    std::string hello = "{\"v\":1,\"client_id\":\"" + util::JsonEscape(util::ToUtf8(clientId)) + "\"}";
    if (!WriteFrame(OP_HANDSHAKE, hello)) {
        lastError_ = "Handshake failed";
        Disconnect();
        return false;
    }

    uint32_t op = 0;
    std::string payload;
    if (ReadFrame(op, payload, 4000) != 1) {
        lastError_ = "No answer from Discord";
        Disconnect();
        return false;
    }
    if (payload.find("\"READY\"") == std::string::npos) {
        std::string msg = util::JsonGetString(payload, "message");
        lastError_ = msg.empty() ? "Discord rejected the application ID" : msg;
        Disconnect();
        return false;
    }
    user_ = util::JsonGetString(payload, "username");
    return true;
}

void DiscordIpc::Disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool DiscordIpc::WriteFrame(uint32_t op, const std::string& payload) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;
    std::string buf(8 + payload.size(), '\0');
    uint32_t len = (uint32_t)payload.size();
    memcpy(&buf[0], &op, 4);
    memcpy(&buf[4], &len, 4);
    if (!payload.empty()) memcpy(&buf[8], payload.data(), payload.size());
    DWORD written = 0;
    if (!WriteFile(pipe_, buf.data(), (DWORD)buf.size(), &written, nullptr) || written != buf.size()) {
        Disconnect();
        return false;
    }
    return true;
}

int DiscordIpc::ReadFrame(uint32_t& op, std::string& payload, DWORD timeoutMs) {
    if (pipe_ == INVALID_HANDLE_VALUE) return -1;
    DWORD start = GetTickCount();

    // wait for a full header
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) { Disconnect(); return -1; }
        if (avail >= 8) break;
        if (GetTickCount() - start >= timeoutMs) return 0;
        Sleep(10);
    }

    uint32_t hdr[2] = {0, 0};
    DWORD got = 0;
    if (!ReadFile(pipe_, hdr, 8, &got, nullptr) || got != 8) { Disconnect(); return -1; }
    op = hdr[0];
    uint32_t len = hdr[1];
    if (len > 1u << 20) { Disconnect(); return -1; }  // sanity limit

    payload.assign(len, '\0');
    uint32_t done = 0;
    DWORD payloadStart = GetTickCount();
    while (done < len) {
        DWORD avail = 0;
        if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) { Disconnect(); return -1; }
        if (avail == 0) {
            if (GetTickCount() - payloadStart > 3000) { Disconnect(); return -1; }
            Sleep(5);
            continue;
        }
        DWORD want = (DWORD)((len - done) < avail ? (len - done) : avail);
        DWORD r = 0;
        if (!ReadFile(pipe_, &payload[done], want, &r, nullptr) || r == 0) { Disconnect(); return -1; }
        done += r;
    }
    return 1;
}

void DiscordIpc::HandleFrame(uint32_t op, const std::string& payload) {
    switch (op) {
        case OP_PING:
            WriteFrame(OP_PONG, payload);
            break;
        case OP_CLOSE: {
            std::string msg = util::JsonGetString(payload, "message");
            lastError_ = msg.empty() ? "Connection closed by Discord" : msg;
            Disconnect();
            break;
        }
        case OP_FRAME:
            if (payload.find("\"evt\":\"ERROR\"") != std::string::npos) {
                lastError_ = util::JsonGetString(payload, "message");
                util::Log(L"Discord error: %S", lastError_.c_str());
            }
            break;
        default:
            break;
    }
}

void DiscordIpc::Pump() {
    for (int guard = 0; guard < 32 && Connected(); ++guard) {
        uint32_t op = 0;
        std::string payload;
        int r = ReadFrame(op, payload, 0);
        if (r != 1) break;
        HandleFrame(op, payload);
    }
}

bool DiscordIpc::SetActivity(const std::string& activityJson) {
    std::string p = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId());
    if (!activityJson.empty()) p += ",\"activity\":" + activityJson;
    p += "},\"nonce\":\"" + std::to_string(++nonce_) + "\"}";
    return WriteFrame(OP_FRAME, p);
}
