#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "discord_ipc.h"

#include <cerrno>
#include <cstring>

#include "util.h"

namespace {
enum : uint32_t { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };

#ifdef _WIN32
// ---- Wine: Linux system calls straight from the DLL ------------------------------------------------------
// Wine executes Windows code natively on Linux, so the Linux kernel ABI is reachable with the syscall
// instruction (x64) / int 0x80 (x86). A tiny machine-code thunk is used because MSVC has no inline assembly
// for x64. Only ever used after util::UnderWine() returned true.

typedef intptr_t(__cdecl* SyscallFn)(intptr_t nr, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4, intptr_t a5,
                                     intptr_t a6);
#ifdef _WIN64
// push rdi; push rsi; mov rax,rcx; mov rdi,rdx; mov rsi,r8; mov rdx,r9; mov r10,[rsp+38h]; mov r8,[rsp+40h];
// mov r9,[rsp+48h]; syscall; pop rsi; pop rdi; ret
const unsigned char kThunk[] = {0x57, 0x56, 0x48, 0x89, 0xC8, 0x48, 0x89, 0xD7, 0x4C, 0x89, 0xC6, 0x4C,
                                0x89, 0xCA, 0x4C, 0x8B, 0x54, 0x24, 0x38, 0x4C, 0x8B, 0x44, 0x24, 0x40,
                                0x4C, 0x8B, 0x4C, 0x24, 0x48, 0x0F, 0x05, 0x5E, 0x5F, 0xC3};
enum : intptr_t { NR_read = 0, NR_close = 3, NR_poll = 7, NR_ioctl = 16, NR_socket = 41, NR_connect = 42,
                  NR_sendto = 44, NR_getuid = 102 };
#else
// push ebp; push ebx; push esi; push edi; mov eax..ebp,[esp+14h..2Ch]; int 80h; pop edi; pop esi; pop ebx; pop ebp; ret
const unsigned char kThunk[] = {0x55, 0x53, 0x56, 0x57, 0x8B, 0x44, 0x24, 0x14, 0x8B, 0x5C, 0x24, 0x18, 0x8B,
                                0x4C, 0x24, 0x1C, 0x8B, 0x54, 0x24, 0x20, 0x8B, 0x74, 0x24, 0x24, 0x8B, 0x7C,
                                0x24, 0x28, 0x8B, 0x6C, 0x24, 0x2C, 0xCD, 0x80, 0x5F, 0x5E, 0x5B, 0x5D, 0xC3};
enum : intptr_t { NR_read = 3, NR_close = 6, NR_poll = 168, NR_ioctl = 54, NR_socket = 359, NR_connect = 362,
                  NR_sendto = 369, NR_getuid = 199 };
#endif
const intptr_t L_AF_UNIX = 1, L_SOCK_STREAM = 1, L_SOCK_CLOEXEC = 02000000, L_MSG_NOSIGNAL = 0x4000,
               L_FIONREAD = 0x541B, L_EINTR = 4;
const short    L_POLLIN = 0x1, L_POLLERR = 0x8, L_POLLNVAL = 0x20;
struct LinuxPollFd { int fd; short events, revents; };
struct LinuxSockAddrUn { unsigned short family; char path[108]; };

SyscallFn Syscall() {
    static SyscallFn fn = []() -> SyscallFn {
        void* mem = VirtualAlloc(nullptr, sizeof(kThunk), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!mem) return nullptr;
        memcpy(mem, kThunk, sizeof(kThunk));
        DWORD old = 0;
        if (!VirtualProtect(mem, sizeof(kThunk), PAGE_EXECUTE_READ, &old)) return nullptr;
        FlushInstructionCache(GetCurrentProcess(), mem, sizeof(kThunk));
        return reinterpret_cast<SyscallFn>(mem);
    }();
    return fn;
}

intptr_t Sys(intptr_t nr, intptr_t a1 = 0, intptr_t a2 = 0, intptr_t a3 = 0, intptr_t a4 = 0, intptr_t a5 = 0,
             intptr_t a6 = 0) {
    for (;;) {
        intptr_t r = Syscall()(nr, a1, a2, a3, a4, a5, a6);
        if (r != -L_EINTR) return r;   // negative values are -errno
    }
}

#endif

}  // namespace

std::vector<std::string> DiscordSocketDirs(const std::string& xdgRuntimeDir, const std::string& tmpDir) {
    std::vector<std::string> dirs;
    auto add = [&](const std::string& d) {
        if (d.empty()) return;
        for (const auto& x : dirs) if (x == d) return;
        dirs.push_back(d);
    };
    if (!xdgRuntimeDir.empty()) {
        add(xdgRuntimeDir);                                          // native package / AppImage / .deb / .rpm
        add(xdgRuntimeDir + "/app/com.discordapp.Discord");          // Flatpak
        add(xdgRuntimeDir + "/app/com.discordapp.DiscordCanary");
        add(xdgRuntimeDir + "/app/dev.vencord.Vesktop");
        add(xdgRuntimeDir + "/snap.discord");                        // Snap
        add(xdgRuntimeDir + "/snap.discord-canary");
    }
    add(tmpDir);
    add("/tmp");
    return dirs;
}

// ------------------------------------------------------------------------------------------- transport

#ifdef _WIN32

bool DiscordIpc::Connected() const { return pipe_ != nullptr || wineFd_ >= 0; }

bool DiscordIpc::OpenChannel() {
    for (int i = 0; i < 10; ++i) {
        std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeW(name.c_str(), 300)) {
            h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (h != INVALID_HANDLE_VALUE) {
            pipe_ = h;
            endpoint_ = util::ToUtf8(name);
            return true;
        }
    }
    // AIMP for Windows running in Wine on Linux: talk to the Linux Discord client directly
    return util::UnderWine() && OpenUnixSocketUnderWine();
}

bool DiscordIpc::OpenUnixSocketUnderWine() {
    if (!Syscall()) return false;
    std::string runtime = util::ToUtf8(util::GetEnv(L"XDG_RUNTIME_DIR"));   // Wine passes the Unix environment
    if (runtime.empty() || runtime[0] != '/') runtime = "/run/user/" + std::to_string((unsigned long)Sys(NR_getuid));
    std::string tmp = util::ToUtf8(util::GetEnv(L"TMPDIR"));
    if (!tmp.empty() && tmp[0] != '/') tmp.clear();
    for (const std::string& dir : DiscordSocketDirs(runtime, tmp)) {
        for (int i = 0; i < 10; ++i) {
            std::string path = dir + "/discord-ipc-" + std::to_string(i);
            LinuxSockAddrUn addr = {};
            if (path.size() >= sizeof(addr.path)) continue;
            addr.family = (unsigned short)L_AF_UNIX;
            memcpy(addr.path, path.c_str(), path.size());
            intptr_t fd = Sys(NR_socket, L_AF_UNIX, L_SOCK_STREAM | L_SOCK_CLOEXEC, 0);
            if (fd < 0) return false;
            if (Sys(NR_connect, fd, (intptr_t)&addr, (intptr_t)sizeof(addr)) == 0) {
                wineFd_ = (long)fd;
                endpoint_ = path + " (Wine -> Linux)";
                util::Log(L"Wine: connected to the Linux Discord socket %ls", util::FromUtf8(path).c_str());
                return true;
            }
            Sys(NR_close, fd);
        }
    }
    return false;
}

void DiscordIpc::Disconnect() {
    if (pipe_) {
        CloseHandle(pipe_);
        pipe_ = nullptr;
    }
    if (wineFd_ >= 0) {
        Sys(NR_close, wineFd_);
        wineFd_ = -1;
    }
}

bool DiscordIpc::IoWrite(const void* data, size_t len) {
    if (pipe_) {
        DWORD written = 0;
        return WriteFile(pipe_, data, (DWORD)len, &written, nullptr) && written == len;
    }
    const char* p = static_cast<const char*>(data);
    while (len > 0) {
        intptr_t n = Sys(NR_sendto, wineFd_, (intptr_t)p, (intptr_t)len, L_MSG_NOSIGNAL, 0, 0);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool DiscordIpc::IoAvailable(size_t& avail) {
    avail = 0;
    if (pipe_) {
        DWORD a = 0;
        if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &a, nullptr)) return false;
        avail = a;
        return true;
    }
    LinuxPollFd pfd = {(int)wineFd_, L_POLLIN, 0};
    intptr_t r = Sys(NR_poll, (intptr_t)&pfd, 1, 0);
    if (r < 0) return false;
    if (r == 0) return true;
    if (pfd.revents & (L_POLLERR | L_POLLNVAL)) return false;
    int a = 0;
    if (Sys(NR_ioctl, wineFd_, L_FIONREAD, (intptr_t)&a) != 0 || a <= 0) return false;   // hung up + empty = closed
    avail = (size_t)a;
    return true;
}

bool DiscordIpc::IoRead(void* data, size_t len, size_t& got) {
    got = 0;
    if (pipe_) {
        DWORD r = 0;
        if (!ReadFile(pipe_, data, (DWORD)len, &r, nullptr)) return false;
        got = r;
        return r > 0;
    }
    intptr_t n = Sys(NR_read, wineFd_, (intptr_t)data, (intptr_t)len);
    if (n <= 0) return false;
    got = (size_t)n;
    return true;
}

#else  // Linux

bool DiscordIpc::Connected() const { return fd_ >= 0; }

bool DiscordIpc::OpenChannel() {
    std::string runtime = util::ToUtf8(util::GetEnv(L"XDG_RUNTIME_DIR"));
    if (runtime.empty()) runtime = "/run/user/" + std::to_string((unsigned)getuid());
    std::string tmp = util::ToUtf8(util::GetEnv(L"TMPDIR"));
    for (const std::string& dir : DiscordSocketDirs(runtime, tmp)) {
        for (int i = 0; i < 10; ++i) {
            std::string path = dir + "/discord-ipc-" + std::to_string(i);
            sockaddr_un addr = {};
            if (path.size() >= sizeof(addr.sun_path)) continue;
            addr.sun_family = AF_UNIX;
            memcpy(addr.sun_path, path.c_str(), path.size());
            int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (s < 0) return false;
            if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                fd_ = s;
                endpoint_ = path;
                return true;
            }
            close(s);
        }
    }
    return false;
}

void DiscordIpc::Disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool DiscordIpc::IoWrite(const void* data, size_t len) {
    const char* p = static_cast<const char*>(data);
    while (len > 0) {
        ssize_t n = send(fd_, p, len, MSG_NOSIGNAL);   // no SIGPIPE if Discord went away
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool DiscordIpc::IoAvailable(size_t& avail) {
    avail = 0;
    pollfd pfd = {fd_, POLLIN, 0};
    int r = poll(&pfd, 1, 0);
    if (r < 0) return errno == EINTR;
    if (r == 0) return true;
    if (pfd.revents & (POLLERR | POLLNVAL)) return false;
    int a = 0;
    if (ioctl(fd_, FIONREAD, &a) != 0 || a <= 0) return false;   // readable / hung up + empty = closed
    avail = (size_t)a;
    return true;
}

bool DiscordIpc::IoRead(void* data, size_t len, size_t& got) {
    got = 0;
    for (;;) {
        ssize_t n = recv(fd_, data, len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        got = (size_t)n;
        return true;
    }
}

#endif

// ------------------------------------------------------------------------------------------- protocol

void DiscordIpc::Fail(IpcError e, const std::string& detail) {
    error_ = e;
    errorDetail_ = detail;
    Disconnect();
}

bool DiscordIpc::Connect(const std::wstring& clientId) {
    Disconnect();
    user_ = DiscordUser();
    error_ = IpcError::None;
    errorDetail_.clear();
    endpoint_.clear();

    if (!OpenChannel()) {
        Fail(IpcError::NotRunning);
        return false;
    }
    std::string hello = "{\"v\":1,\"client_id\":\"" + util::JsonEscape(util::ToUtf8(clientId)) + "\"}";
    if (!WriteFrame(OP_HANDSHAKE, hello)) {
        Fail(IpcError::Handshake);
        return false;
    }
    uint32_t op = 0;
    std::string payload;
    if (ReadFrame(op, payload, 4000) != 1) {
        Fail(IpcError::NoAnswer);
        return false;
    }
    if (payload.find("\"READY\"") == std::string::npos) {
        Fail(IpcError::Rejected, util::JsonGetString(payload, "message"));
        return false;
    }
    user_.id = util::JsonAfter(payload, "\"user\"", "id");
    user_.name = util::JsonAfter(payload, "\"user\"", "username");
    user_.globalName = util::JsonAfter(payload, "\"user\"", "global_name");
    user_.avatar = util::JsonAfter(payload, "\"user\"", "avatar");
    return true;
}

bool DiscordIpc::WriteFrame(uint32_t op, const std::string& payload) {
    if (!Connected()) return false;
    std::string buf(8 + payload.size(), '\0');
    uint32_t len = (uint32_t)payload.size();
    memcpy(&buf[0], &op, 4);    // the protocol is little endian, like every supported platform
    memcpy(&buf[4], &len, 4);
    if (!payload.empty()) memcpy(&buf[8], payload.data(), payload.size());
    if (!IoWrite(buf.data(), buf.size())) {
        Disconnect();
        return false;
    }
    return true;
}

int DiscordIpc::ReadFrame(uint32_t& op, std::string& payload, uint32_t timeoutMs) {
    if (!Connected()) return -1;
    const uint64_t start = util::TickMs();

    // wait for a full header
    for (;;) {
        size_t avail = 0;
        if (!IoAvailable(avail)) { Disconnect(); return -1; }
        if (avail >= 8) break;
        if (util::TickMs() - start >= timeoutMs) return 0;
        util::SleepMs(10);
    }

    uint8_t hdr[8];
    size_t got = 0, have = 0;
    while (have < 8) {
        if (!IoRead(hdr + have, 8 - have, got)) { Disconnect(); return -1; }
        have += got;
    }
    memcpy(&op, hdr, 4);
    uint32_t len;
    memcpy(&len, hdr + 4, 4);
    if (len > 1u << 20) { Disconnect(); return -1; }  // sanity limit

    payload.assign(len, '\0');
    uint32_t done = 0;
    const uint64_t payloadStart = util::TickMs();
    while (done < len) {
        size_t avail = 0;
        if (!IoAvailable(avail)) { Disconnect(); return -1; }
        if (avail == 0) {
            if (util::TickMs() - payloadStart > 3000) { Disconnect(); return -1; }
            util::SleepMs(5);
            continue;
        }
        size_t want = (len - done) < avail ? (len - done) : avail;
        if (!IoRead(&payload[done], want, got)) { Disconnect(); return -1; }
        done += (uint32_t)got;
    }
    return 1;
}

void DiscordIpc::HandleFrame(uint32_t op, const std::string& payload) {
    switch (op) {
        case OP_PING:
            WriteFrame(OP_PONG, payload);
            break;
        case OP_CLOSE:
            Fail(IpcError::Closed, util::JsonGetString(payload, "message"));
            break;
        case OP_FRAME:
            if (util::JsonGetString(payload, "evt") == "ERROR")
                util::Log(L"Discord error: %ls", util::FromUtf8(util::JsonGetString(payload, "message")).c_str());
            else if (util::JsonGetString(payload, "cmd") == "SET_ACTIVITY")
                shownImage_ = util::JsonGetString(payload, "large_image");
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
    std::string p = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(util::ProcessId());
    if (!activityJson.empty()) p += ",\"activity\":" + activityJson;
    p += "},\"nonce\":\"" + std::to_string(++nonce_) + "\"}";
    return WriteFrame(OP_FRAME, p);
}
