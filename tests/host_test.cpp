// Mock AIMP host: loads the plugin the way AIMP does, plays a fake track through the player service and lets
// the plugin talk to tests/fake_discord.py (Discord) and tests/fake_web.py (GitHub, Discord CDN, Deezer).
//   Linux:   host_test aimp_discord_rpc.so    (events through the message hook, UI refresh through the thread service)
//   Windows: host_test.exe aimp_discord_rpc.dll (timer window; run under Wine against the Linux fake socket)
// It opens the plugin's settings page through a mock of AIMP's UI service (tests/mock_ui.h), changes values like a
// user would, renders the live preview into an image, exports / imports the settings, sends a test presence,
// reconnects, and checks that the automatic update check downloads the package, opens it "in AIMP" and restarts
// AIMP once the package is installed.
// "AIMP" is this program: started with a .aimppack argument it notes the file (AIMP_TEST_MARKER), "installs" it like
// AIMP (AIMP_TEST_INSTALL: the plugin file is renamed to .old and a new copy takes its place) and exits.
//
// Environment: XDG_RUNTIME_DIR (fake Discord socket), XDG_CONFIG_HOME / APPDATA (settings), AIMP_DISCORD_RPC_TEST_URL
// (fake web server), AIMP_TEST_MARKER, AIMP_TEST_OUT (folder for preview images / exported settings),
// AIMP_TEST_LANGUAGE (what AIMP's MUI service reports, default "English"), AIMP_TEST_UI_ONLY=1 (settings page only),
// AIMP_TEST_INSTALL (plugin file that "AIMP" replaces when it opens the update package), AIMP_TEST_PROFILE (what
// AIMP reports as its profile folder).

#ifdef _WIN32
#include <windows.h>
#endif

#include "apiCore.h"
#include "apiFileManager.h"
#include "apiGUI.h"
#include "apiMUI.h"
#include "apiMessages.h"
#include "apiOptions.h"
#include "apiPlayer.h"
#include "apiPlaylists.h"
#include "apiPlugin.h"
#include "apiThreading.h"

#include "../src/version.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

#ifdef _WIN32
#define T(s) L##s
using Str = std::wstring;
Str FromUtf8(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}
std::string Narrow(const Str& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
#else
#define T(s) s
using Str = std::string;
Str FromUtf8(const char* s) { return s; }
std::string Narrow(const Str& s) { return s; }
#endif

namespace {

bool Same(REFIID a, REFIID b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }

std::string Env(const char* name, const char* def = "") {   // UTF-8
#ifdef _WIN32
    const wchar_t* v = _wgetenv(FromUtf8(name).c_str());
    return v ? Narrow(v) : def;
#else
    const char* v = getenv(name);
    return v ? v : def;
#endif
}

template <class I>
class Obj : public I {
public:
    virtual ~Obj() = default;
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IUnknown)) { *ppv = this; this->AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return (DWORD)++ref_; }
    DWORD __unknwncall Release() override {
        DWORD r = (DWORD)--ref_;
        if (r == 0) delete this;
        return r;
    }
private:
    std::atomic<long> ref_{1};
};

class String : public Obj<IAIMPString> {
public:
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPString)) { *ppv = static_cast<IAIMPString*>(this); AddRef(); return S_OK; }
        return Obj<IAIMPString>::QueryInterface(riid, ppv);
    }
    Str s;
    HRESULT WINAPI Get(INT32, TChar*) override { return E_NOTIMPL; }
    PChar WINAPI GetData() override { return &s[0]; }
    INT32 WINAPI GetLength() override { return (INT32)s.size(); }
    INT32 WINAPI GetHashCode() override { return 0; }
    HRESULT WINAPI Set(INT32, TChar) override { return E_NOTIMPL; }
    HRESULT WINAPI SetData(PChar d, INT32 n) override { s.assign(d, (size_t)n); return S_OK; }
    HRESULT WINAPI Add(IAIMPString*) override { return E_NOTIMPL; }
    HRESULT WINAPI Add2(PChar, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI ChangeCase(INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Clone(IAIMPString**) override { return E_NOTIMPL; }
    HRESULT WINAPI Compare(IAIMPString*, INT32*, BOOL) override { return E_NOTIMPL; }
    HRESULT WINAPI Compare2(PChar, INT32, INT32*, BOOL) override { return E_NOTIMPL; }
    HRESULT WINAPI Delete(INT32, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Find(IAIMPString*, INT32*, INT32, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Find2(TChar*, INT32, INT32*, INT32, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Insert(INT32, IAIMPString*) override { return E_NOTIMPL; }
    HRESULT WINAPI Insert2(INT32, TChar*, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Replace(IAIMPString*, IAIMPString*, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Replace2(TChar*, INT32, TChar*, INT32, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI SubString(INT32, INT32, IAIMPString**) override { return E_NOTIMPL; }
};

IAIMPString* MakeStr(const Str& s) {
    String* r = new String();
    r->s = s;
    return r;
}

}  // namespace

namespace mockui {
IAIMPString* NewString(const std::basic_string<TChar>& s) { return MakeStr(s); }
}  // namespace mockui
#include "mock_ui.h"

namespace {

// ---- property list with a few strings (file info, playlist item, playlist)
template <class I>
class Props : public Obj<I> {
public:
    std::map<int, Str> text;
    std::map<int, IUnknown*> objects;   // not owned (the test keeps them alive)
    double duration = 0;
    void WINAPI BeginUpdate() override {}
    void WINAPI EndUpdate() override {}
    HRESULT WINAPI Reset() override { return S_OK; }
    HRESULT WINAPI GetValueAsFloat(INT32 id, DOUBLE* v) override {
        if (id != AIMP_FILEINFO_PROPID_DURATION || duration <= 0) return E_FAIL;
        *v = duration;
        return S_OK;
    }
    HRESULT WINAPI GetValueAsInt32(INT32, INT32*) override { return E_FAIL; }
    HRESULT WINAPI GetValueAsInt64(INT32, INT64*) override { return E_FAIL; }
    HRESULT WINAPI GetValueAsObject(INT32 id, CONSTIID iid, void** v) override {
        auto o = objects.find(id);
        if (o != objects.end()) return o->second->QueryInterface(iid, v);
        auto it = text.find(id);
        if (it == text.end() || !Same(iid, IID_IAIMPString)) return E_FAIL;
        *v = MakeStr(it->second);
        return S_OK;
    }
    HRESULT WINAPI SetValueAsFloat(INT32, const DOUBLE) override { return E_NOTIMPL; }
    HRESULT WINAPI SetValueAsInt32(INT32, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI SetValueAsInt64(INT32, const INT64) override { return E_NOTIMPL; }
    HRESULT WINAPI SetValueAsObject(INT32, IUnknown*) override { return E_NOTIMPL; }
};

class FileInfo : public Props<IAIMPFileInfo> {
public:
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPFileInfo)) { *ppv = static_cast<IAIMPFileInfo*>(this); AddRef(); return S_OK; }
        return Props<IAIMPFileInfo>::QueryInterface(riid, ppv);
    }
    HRESULT WINAPI Assign(IAIMPFileInfo*) override { return E_NOTIMPL; }
    HRESULT WINAPI Clone(IAIMPFileInfo**) override { return E_NOTIMPL; }
};

class PlaylistProps : public Props<IAIMPPropertyList> {
public:
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPPropertyList)) { *ppv = static_cast<IAIMPPropertyList*>(this); AddRef(); return S_OK; }
        return Props<IAIMPPropertyList>::QueryInterface(riid, ppv);
    }
};

class Playlist : public Obj<IAIMPPlaylist> {
public:
    PlaylistProps* props = new PlaylistProps();
    ~Playlist() override { props->Release(); }
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPPlaylist)) { *ppv = static_cast<IAIMPPlaylist*>(this); AddRef(); return S_OK; }
        if (Same(riid, IID_IAIMPPropertyList)) return props->QueryInterface(riid, ppv);
        return Obj<IAIMPPlaylist>::QueryInterface(riid, ppv);
    }
    HRESULT WINAPI Add(IUnknown*, DWORD, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI AddList(IAIMPObjectList*, DWORD, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Delete(IAIMPPlaylistItem*) override { return E_NOTIMPL; }
    HRESULT WINAPI Delete2(INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Delete3(DWORD, TAIMPPlaylistDeleteProc, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI DeleteAll() override { return E_NOTIMPL; }
    HRESULT WINAPI Sort(INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI Sort2(IAIMPString*) override { return E_NOTIMPL; }
    HRESULT WINAPI Sort3(TAIMPPlaylistCompareProc*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI BeginUpdate() override { return S_OK; }
    HRESULT WINAPI EndUpdate() override { return S_OK; }
    HRESULT WINAPI Close(DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI GetFiles(DWORD, IAIMPObjectList**) override { return E_NOTIMPL; }
    HRESULT WINAPI MergeGroup(IAIMPPlaylistGroup*) override { return E_NOTIMPL; }
    HRESULT WINAPI ReloadFromPreimage() override { return E_NOTIMPL; }
    HRESULT WINAPI ReloadInfo(DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI GetItem(INT32, CONSTIID, void**) override { return E_NOTIMPL; }
    INT32 WINAPI GetItemCount() override { return 1; }
    HRESULT WINAPI GetGroup(INT32, CONSTIID, void**) override { return E_NOTIMPL; }
    INT32 WINAPI GetGroupCount() override { return 0; }
    HRESULT WINAPI ListenerAdd(IAIMPPlaylistListener*) override { return E_NOTIMPL; }
    HRESULT WINAPI ListenerRemove(IAIMPPlaylistListener*) override { return E_NOTIMPL; }
};

class PlaylistItem : public Props<IAIMPPlaylistItem> {
public:
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPPlaylistItem)) { *ppv = static_cast<IAIMPPlaylistItem*>(this); AddRef(); return S_OK; }
        return Props<IAIMPPlaylistItem>::QueryInterface(riid, ppv);
    }
    HRESULT WINAPI ReloadInfo() override { return S_OK; }
};

class Player : public Obj<IAIMPServicePlayer> {
public:
    int state = 0;
    double pos = 0, dur = 0;
    FileInfo* info = nullptr;
    PlaylistItem* item = nullptr;
    HRESULT WINAPI Play(IAIMPPlaybackQueueItem*) override { return E_NOTIMPL; }
    HRESULT WINAPI Play2(IAIMPPlaylistItem*) override { return E_NOTIMPL; }
    HRESULT WINAPI Play3(IAIMPPlaylist*) override { return E_NOTIMPL; }
    HRESULT WINAPI Play4(IAIMPString*, DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI GoToNext() override { return E_NOTIMPL; }
    HRESULT WINAPI GoToPrev() override { return E_NOTIMPL; }
    HRESULT WINAPI GetDuration(DOUBLE* s) override { *s = dur; return S_OK; }
    HRESULT WINAPI GetPosition(DOUBLE* s) override { *s = pos; return S_OK; }
    HRESULT WINAPI SetPosition(const DOUBLE) override { return E_NOTIMPL; }
    HRESULT WINAPI GetMute(BOOL*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetMute(const BOOL) override { return E_NOTIMPL; }
    HRESULT WINAPI GetVolume(SINGLE*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetVolume(const SINGLE) override { return E_NOTIMPL; }
    HRESULT WINAPI GetInfo(IAIMPFileInfo** fi) override {
        ++infoReads;   // how often the plugin reads the track's tags (cost per poll)
        if (!info) return E_FAIL;
        info->AddRef();
        *fi = info;
        return S_OK;
    }
    HRESULT WINAPI GetPlaylistItem(IAIMPPlaylistItem** it) override {
        if (!item || state == 0) return E_FAIL;
        item->AddRef();
        *it = item;
        return S_OK;
    }
    INT32 WINAPI GetState() override { return state; }
    int infoReads = 0;
    HRESULT WINAPI Pause() override { return E_NOTIMPL; }
    HRESULT WINAPI Resume() override { return E_NOTIMPL; }
    HRESULT WINAPI Stop() override { return E_NOTIMPL; }
    HRESULT WINAPI StopAfterTrack() override { return E_NOTIMPL; }
};

class Dispatcher : public Obj<IAIMPServiceMessageDispatcher> {
public:
    IAIMPMessageHook* hook = nullptr;
    Str notification;   // last AIMP_MSG_CMD_SHOW_NOTIFICATION text
    HRESULT WINAPI Send(DWORD msg, INT32, void* param2) override {
        if (msg == (DWORD)AIMP_MSG_CMD_SHOW_NOTIFICATION && param2) notification = static_cast<TChar*>(param2);
        return E_NOTIMPL;
    }
    DWORD WINAPI Register(PChar) override { return 0; }
    HRESULT WINAPI Hook(IAIMPMessageHook* h) override { h->AddRef(); hook = h; return S_OK; }
    HRESULT WINAPI Unhook(IAIMPMessageHook* h) override {
        if (h != hook) return E_FAIL;
        hook = nullptr;
        h->Release();
        return S_OK;
    }
    void Fire(DWORD msg) {
        HRESULT r = S_OK;
        if (hook) hook->CoreMessage(msg, 0, nullptr, &r);
    }
};

// ---- memory stream + image (the settings page shows covers / avatars through AIMP's image objects)
class MemoryStream : public Obj<IAIMPMemoryStream> {
public:
    std::string data;
    size_t pos = 0;
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPMemoryStream) || Same(riid, IID_IAIMPStream)) {
            *ppv = static_cast<IAIMPMemoryStream*>(this);
            AddRef();
            return S_OK;
        }
        return Obj<IAIMPMemoryStream>::QueryInterface(riid, ppv);
    }
    INT64 WINAPI GetSize() override { return (INT64)data.size(); }
    HRESULT WINAPI SetSize(const INT64 v) override { data.resize((size_t)v); return S_OK; }
    INT64 WINAPI GetPosition() override { return (INT64)pos; }
    HRESULT WINAPI Seek(const INT64 offset, INT32 mode) override {
        INT64 base = mode == AIMP_STREAM_SEEKMODE_FROM_CURRENT ? (INT64)pos : mode == AIMP_STREAM_SEEKMODE_FROM_END ? (INT64)data.size() : 0;
        pos = (size_t)(base + offset);
        return S_OK;
    }
    INT32 WINAPI Read(void* buf, DWORD count) override {
        size_t n = pos < data.size() ? std::min<size_t>(count, data.size() - pos) : 0;
        memcpy(buf, data.data() + pos, n);
        pos += n;
        return (INT32)n;
    }
    HRESULT WINAPI Write(void* buf, DWORD count, DWORD* written) override {
        if (pos + count > data.size()) data.resize(pos + count);
        memcpy(&data[pos], buf, count);
        pos += count;
        if (written) *written = count;
        return S_OK;
    }
    void* WINAPI GetData() override { return &data[0]; }
};

std::atomic<int> g_images{0}, g_imageDraws{0};

class Image : public Obj<IAIMPImage> {
public:
    uint32_t color = 0x808080;   // derived from the bytes: different pictures get different colours
    Image() { ++g_images; }
    ~Image() override { --g_images; }
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPImage)) { *ppv = static_cast<IAIMPImage*>(this); AddRef(); return S_OK; }
        return Obj<IAIMPImage>::QueryInterface(riid, ppv);
    }
    HRESULT WINAPI LoadFromFile(IAIMPString*) override { return E_NOTIMPL; }
    HRESULT WINAPI LoadFromStream(IAIMPStream* s) override {
        std::string b;
        char buf[4096];
        INT32 n;
        while ((n = s->Read(buf, sizeof buf)) > 0) b.append(buf, (size_t)n);
        if (b.size() < 8 || (memcmp(b.data(), "\x89PNG", 4) != 0 && (unsigned char)b[0] != 0xFF)) return E_FAIL;
        uint32_t h = 2166136261u;
        for (unsigned char c : b) h = (h ^ c) * 16777619u;
        color = (h & 0x7f7f7f) | 0x404040;
        return S_OK;
    }
    HRESULT WINAPI SaveToFile(IAIMPString*, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI SaveToStream(IAIMPStream*, INT32) override { return E_NOTIMPL; }
    INT32 WINAPI GetFormatID() override { return AIMP_IMAGE_FORMAT_PNG; }
    HRESULT WINAPI GetSize(SIZE* size) override { size->cx = size->cy = 64; return S_OK; }
    HRESULT Clone(IAIMPImage**) override { return E_NOTIMPL; }   // (declared without WINAPI in the SDK)
    HRESULT WINAPI Draw(HCANVAS canvas, RECT r, DWORD, IUnknown*) override {
        ++g_imageDraws;
#ifdef _WIN32
        HBRUSH b = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
        FillRect(canvas, &r, b);
        DeleteObject(b);
#else
        cairo_rectangle(canvas, r.left, r.top, r.right - r.left, r.bottom - r.top);
        cairo_set_source_rgb(canvas, ((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0, (color & 255) / 255.0);
        cairo_fill(canvas);
#endif
        return S_OK;
    }
    HRESULT WINAPI Resize(INT32, INT32) override { return S_OK; }
};

// ---- thread service (Linux build: background results refresh the settings page on the main thread)
class Threads : public Obj<IAIMPServiceThreads> {
public:
    std::mutex mu;
    std::deque<IAIMPTask*> queue;
    int executed = 0;
    HRESULT WINAPI ExecuteInMainThread(IAIMPTask* task, DWORD) override {
        task->AddRef();
        std::lock_guard<std::mutex> lk(mu);
        queue.push_back(task);
        return S_OK;
    }
    HRESULT WINAPI ExecuteInThread(IAIMPTask*, TTaskHandle*) override { return E_NOTIMPL; }
    HRESULT WINAPI Cancel(TTaskHandle, DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI WaitFor(TTaskHandle) override { return E_NOTIMPL; }
    void RunPending() {   // main thread
        for (;;) {
            IAIMPTask* t = nullptr;
            {
                std::lock_guard<std::mutex> lk(mu);
                if (queue.empty()) return;
                t = queue.front();
                queue.pop_front();
            }
            t->Execute(nullptr);
            t->Release();
            ++executed;
        }
    }
};

// ---- AIMP's language service
class Mui : public Obj<IAIMPServiceMUI> {
public:
    Str name = FromUtf8(Env("AIMP_TEST_LANGUAGE", "English").c_str());
    HRESULT WINAPI GetName(IAIMPString** v) override { *v = MakeStr(name); return S_OK; }
    HRESULT WINAPI GetValue(IAIMPString*, IAIMPString**) override { return E_FAIL; }
    HRESULT WINAPI GetValuePart(IAIMPString*, INT32, IAIMPString**) override { return E_FAIL; }
};

// ---- AIMP's shutdown service: the plugin restarts AIMP once its update is installed
class ShutdownService : public Obj<IAIMPServiceShutdown> {
public:
    std::atomic<int> restarts{0};
    HRESULT WINAPI Restart(IAIMPString*) override { ++restarts; return S_OK; }
    HRESULT WINAPI Shutdown(DWORD) override { return E_NOTIMPL; }
};

// ---- AIMP's message window: the plugin shows "update installed" with it
class MessageDialog : public Obj<IAIMPUIMessageDialog> {
public:
    mockui::TStr caption, text;
    int shown = 0;
    HRESULT WINAPI Execute(HWND, IAIMPString* c, IAIMPString* t, DWORD) override {
        caption = c ? mockui::TStr(c->GetData(), c->GetLength()) : mockui::TStr();
        text = t ? mockui::TStr(t->GetData(), t->GetLength()) : mockui::TStr();
        ++shown;
        return S_OK;
    }
};

class Core : public Obj<IAIMPCore> {
public:
    Player* player = new Player();
    Dispatcher* dispatcher = new Dispatcher();
    Threads* threads = new Threads();
    Mui* mui = new Mui();
    ShutdownService* shutdown = new ShutdownService();
    MessageDialog* message = new MessageDialog();
    mockui::Service ui;
    mockui::OptionsService options;
    IAIMPOptionsDialogFrame* frame = nullptr;
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPServicePlayer)) { player->AddRef(); *ppv = player; return S_OK; }
        if (Same(riid, IID_IAIMPServiceMessageDispatcher)) { dispatcher->AddRef(); *ppv = dispatcher; return S_OK; }
        if (Same(riid, IID_IAIMPServiceThreads)) { threads->AddRef(); *ppv = threads; return S_OK; }
        if (Same(riid, IID_IAIMPServiceMUI)) { mui->AddRef(); *ppv = mui; return S_OK; }
        if (Same(riid, IID_IAIMPServiceShutdown)) { shutdown->AddRef(); *ppv = shutdown; return S_OK; }
        if (Same(riid, IID_IAIMPUIMessageDialog)) { message->AddRef(); *ppv = message; return S_OK; }
        if (Same(riid, IID_IAIMPServiceUI)) { *ppv = static_cast<IAIMPServiceUI*>(&ui); return S_OK; }
        if (Same(riid, IID_IAIMPServiceOptionsDialog)) { *ppv = static_cast<IAIMPServiceOptionsDialog*>(&options); return S_OK; }
        return Obj<IAIMPCore>::QueryInterface(riid, ppv);
    }
    HRESULT WINAPI CreateObject(CONSTIID iid, void** obj) override {
        if (Same(iid, IID_IAIMPString)) { *obj = static_cast<IAIMPString*>(new String()); return S_OK; }
        if (Same(iid, IID_IAIMPMemoryStream)) { *obj = static_cast<IAIMPMemoryStream*>(new MemoryStream()); return S_OK; }
        if (Same(iid, IID_IAIMPImage)) { *obj = static_cast<IAIMPImage*>(new Image()); return S_OK; }
        return E_NOTIMPL;
    }
    HRESULT WINAPI GetPath(int id, IAIMPString** v) override {   // AIMP_TEST_PROFILE: AIMP's profile folder
        const std::string p = Env("AIMP_TEST_PROFILE");
        if (id != AIMP_CORE_PATH_PROFILE || p.empty()) return E_NOTIMPL;
        *v = MakeStr(FromUtf8(p.c_str()));
        return S_OK;
    }
    HRESULT WINAPI RegisterExtension(CONSTIID iid, IUnknown* ext) override {
        if (Same(iid, IID_IAIMPServiceOptionsDialog) && ext)
            ext->QueryInterface(IID_IAIMPOptionsDialogFrame, reinterpret_cast<void**>(&frame));
        return S_OK;
    }
    HRESULT WINAPI RegisterService(IUnknown*) override { return E_NOTIMPL; }
    HRESULT WINAPI UnregisterExtension(IUnknown* ext) override {
        if (frame && ext) { frame->Release(); frame = nullptr; }
        return S_OK;
    }
};

Core* g_core = nullptr;

void Wait500ms() {
#ifdef _WIN32
    // the plugin polls from a WM_TIMER on this (main) thread
    DWORD end = GetTickCount() + 500;
    while ((int)(end - GetTickCount()) > 0) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        Sleep(10);
    }
#else
    for (int i = 0; i < 10; ++i) {
        usleep(50 * 1000);
        g_core->threads->RunPending();   // AIMP runs queued tasks on its main thread
    }
#endif
}

void Tick(Core* core, int seconds) {   // AIMP fires UPDATE_POSITION once per second while playing
    for (int i = 0; i < seconds * 2; ++i) {
        Wait500ms();
        if (core->player->state == 2) core->player->pos += 0.5;
        core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_UPDATE_POSITION);
    }
}

int Fail(const char* what) {
    fprintf(stderr, "FAILED: %s\n", what);
    return 1;
}

using Info = mockui::Registry::Info;
using mockui::TStr;

Str S(const char* utf8) { return FromUtf8(utf8); }

// first control of the given kind whose property 'prop' equals 'text'
bool FindControl(Core* core, const char* kind, int prop, const TStr& text, Info& out) {
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (i.kind != mockui::Service::K(kind)) continue;
        auto it = i.strs->find(prop);
        if (it != i.strs->end() && it->second == text) { out = i; return true; }
    }
    return false;
}

bool FindKind(Core* core, const char* kind, Info& out, int nth = 0) {
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (i.kind == mockui::Service::K(kind) && nth-- == 0) { out = i; return true; }
    }
    return false;
}

bool ContainsLabel(Core* core, const TStr& part, TStr* full = nullptr) {
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (i.kind != mockui::Service::K("Label")) continue;
        auto it = i.strs->find(AIMPUI_LABEL_PROPID_TEXT);
        if (it != i.strs->end() && it->second.find(part) != TStr::npos) {
            if (full) *full = it->second;
            return true;
        }
    }
    return false;
}

bool WaitForLabel(Core* core, const TStr& part, int seconds, TStr* full = nullptr) {
    for (int i = 0; i < seconds * 2; ++i) {
        if (ContainsLabel(core, part, full)) return true;
        Wait500ms();
        core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_UPDATE_POSITION);
    }
    return ContainsLabel(core, part, full);
}

// No control text may look like an untranslated key ("Gen.Enable"): every text must come from the language file.
bool AllTranslated(Core* core) {
    static const char* prefixes[] = {"Tab.", "Gen.", "Disp.", "Cov.", "Src.", "Adv.", "About.", "Upd.", "St.", "Prev."};
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        std::vector<TStr> texts = *i.items;
        for (auto& kv : *i.strs) texts.push_back(kv.second);
        for (const TStr& t : texts)
            for (const char* p : prefixes)
                if (t.rfind(S(p), 0) == 0 && t.find(' ') == TStr::npos) {
                    fprintf(stderr, "untranslated text: %s\n", Narrow(t).c_str());
                    return false;
                }
    }
    return true;
}

// ---- preview rendering into an image file (to look at it, and to see that drawing works)
#ifdef _WIN32
#define kImageExt ".bmp"   // GDI+ draws into a DIB section, saved as BMP
#else
#define kImageExt ".png"   // cairo image surface
#endif
bool RenderPreview(Core* core, const std::string& path, int scale, int nth = 0) {   // nth paint box: 0 preview, 1 avatar
    Info paint;
    if (!FindKind(core, "Paint", paint, nth)) return false;
    const int w = (paint.bounds.right - paint.bounds.left) * scale, h = (paint.bounds.bottom - paint.bounds.top) * scale;
    RECT r = {0, 0, w, h};
#ifdef _WIN32
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    HBRUSH white = CreateSolidBrush(RGB(240, 240, 240));   // the skin's background
    FillRect(dc, &r, white);
    DeleteObject(white);
    bool ok = paint.paint(dc, r);
    GdiFlush();
    FILE* f = fopen(path.c_str(), "wb");
    if (f) {
        BITMAPFILEHEADER fh = {};
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
        fh.bfSize = fh.bfOffBits + w * h * 4;
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        fwrite(bits, 4, (size_t)w * h, f);
        fclose(f);
    }
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
#else
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 0.94, 0.94, 0.94);   // the skin's background
    cairo_paint(cr);
    bool ok = paint.paint(cr, r);
    cairo_destroy(cr);
    cairo_surface_write_to_png(surface, path.c_str());
    cairo_surface_destroy(surface);
#endif
    printf("preview (%dx%d) written to %s\n", w, h, path.c_str());
    return ok;
}

#ifndef _WIN32
// Draws every tab of the settings page as a simple mock-up (AIMP draws the real controls): shows the layout and
// marks texts in red that are wider than their control in the current language.
int g_overflows = 0;

void LayoutText(cairo_t* cr, const TStr& t, double x, double y, double w, double h, bool wrap, bool center, bool link) {
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgb(cr, link ? 0.1 : 0.12, link ? 0.4 : 0.12, link ? 0.8 : 0.12);
    cairo_text_extents_t e;
    std::vector<std::string> lines;
    if (wrap) {
        std::string line, word, all = t + " ";
        for (char ch : all) {
            if (ch != ' ') { word += ch; continue; }
            std::string next = line.empty() ? word : line + " " + word;
            cairo_text_extents(cr, next.c_str(), &e);
            if (!line.empty() && e.x_advance > w) { lines.push_back(line); line = word; }
            else line = next;
            word.clear();
        }
        if (!line.empty()) lines.push_back(line);
    } else {
        lines.push_back(t);
    }
    bool over = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        cairo_text_extents(cr, lines[i].c_str(), &e);
        if (e.x_advance > w + 0.5 || (wrap && (i + 1) * 15 > h + 3)) over = true;
        double tx = center ? x + (w - e.x_advance) / 2 : x;
        cairo_move_to(cr, tx, y + 12 + i * 15 + (wrap ? 0 : (h - 15) / 2));
        cairo_show_text(cr, lines[i].c_str());
    }
    cairo_restore(cr);
    if (over) {
        ++g_overflows;
        cairo_set_source_rgb(cr, 0.9, 0.1, 0.1);
        cairo_set_line_width(cr, 1.5);
        cairo_rectangle(cr, x - 1, y - 1, w + 2, h + 2);
        cairo_stroke(cr);
        fprintf(stderr, "text too wide: %s\n", t.c_str());
    }
}

void RenderLayout(Core* core, const std::string& dir) {
    int page = 0;
    for (auto& getSheet : core->ui.reg.all) {
        Info sheet = getSheet();
        if (sheet.kind != mockui::Service::K("S")) continue;
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 490, 452);
        cairo_t* cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 0.97, 0.97, 0.97);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, 0.2, 0.45, 0.6);   // tab caption
        cairo_rectangle(cr, 0, 0, 490, 24);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 13);
        cairo_move_to(cr, 8, 17);
        cairo_show_text(cr, (*sheet.strs)[AIMPUI_TABSHEET_PROPID_CAPTION].c_str());
        cairo_translate(cr, 5, 29);
        for (auto& get : core->ui.reg.all) {
            Info c = get();
            if (c.parent != sheet.self || !c.placed) continue;
            const double x = c.bounds.left, y = c.bounds.top, w = c.bounds.right - c.bounds.left,
                         h = c.bounds.bottom - c.bounds.top;
            auto prop = [&](int id) { auto it = c.ints->find(id); return it == c.ints->end() ? -1LL : it->second; };
            auto str = [&](int id) { auto it = c.strs->find(id); return it == c.strs->end() ? TStr() : it->second; };
            if (prop(AIMPUI_CONTROL_PROPID_VISIBLE) == 0) {   // hidden: dashed outline
                double dash = 3;
                cairo_set_dash(cr, &dash, 1, 0);
                cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
                cairo_set_line_width(cr, 1);
                cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
                cairo_stroke(cr);
                cairo_set_dash(cr, nullptr, 0, 0);
                continue;
            }
            const bool disabled = prop(AIMPUI_CONTROL_PROPID_ENABLED) == 0;
            auto box = [&](double r, double g, double b) {
                cairo_set_source_rgb(cr, r, g, b);
                cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
                cairo_fill_preserve(cr);
                cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
                cairo_set_line_width(cr, 1);
                cairo_stroke(cr);
            };
            const std::string k = c.kind;
            if (k == "Label") {
                LayoutText(cr, str(AIMPUI_LABEL_PROPID_TEXT), x, y, w, h, prop(AIMPUI_LABEL_PROPID_WORDWRAP) == 1, false,
                           !str(AIMPUI_LABEL_PROPID_URL).empty());
            } else if (k == "Check") {
                box(1, 1, 1);
                cairo_set_source_rgb(cr, 0.97, 0.97, 0.97);
                cairo_rectangle(cr, x, y, w, h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
                cairo_rectangle(cr, x + 1.5, y + 3.5, 12, 12);
                cairo_stroke(cr);
                if (prop(AIMPUI_CHECKBOX_PROPID_STATE) == 1) {
                    cairo_move_to(cr, x + 4, y + 9);
                    cairo_line_to(cr, x + 7, y + 13);
                    cairo_line_to(cr, x + 12, y + 5);
                    cairo_stroke(cr);
                }
                LayoutText(cr, str(AIMPUI_CHECKBOX_PROPID_CAPTION), x + 19, y, w - 19, h, false, false, false);
            } else if (k == "Edit") {   // (edits scroll: no width check)
                box(1, 1, 1);
                const int before = g_overflows;
                LayoutText(cr, str(AIMPUI_BASEEDIT_PROPID_TEXT), x + 4, y, std::max(0.0, w - 8), h, false, false, false);
                g_overflows = before;
            } else if (k == "Combo") {
                box(1, 1, 1);
                long long sel = prop(AIMPUI_COMBOBOX_PROPID_ITEMINDEX);
                cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, 12);
                for (const TStr& item : *c.items) {   // every item must fit the closed combo box
                    cairo_text_extents_t e;
                    cairo_text_extents(cr, item.c_str(), &e);
                    if (e.x_advance > w - 22) {
                        ++g_overflows;
                        fprintf(stderr, "combo item too wide: %s\n", item.c_str());
                    }
                }
                if (sel >= 0 && sel < (long long)c.items->size())
                    LayoutText(cr, (*c.items)[(size_t)sel], x + 4, y, w - 22, h, false, false, false);
                cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
                cairo_move_to(cr, x + w - 14, y + h / 2 - 2);
                cairo_line_to(cr, x + w - 6, y + h / 2 - 2);
                cairo_line_to(cr, x + w - 10, y + h / 2 + 3);
                cairo_fill(cr);
            } else if (k == "Button") {
                box(0.88, 0.88, 0.88);
                LayoutText(cr, str(AIMPUI_BUTTON_PROPID_CAPTION), x + 4, y, w - 8, h, false, true, false);
            } else if (k == "Memo") {
                box(1, 1, 1);
                TStr text = str(AIMPUI_BASEEDIT_PROPID_TEXT);
                for (const TStr& l : *c.lines) text += l + "\n";
                cairo_save(cr);
                cairo_rectangle(cr, x, y, w, h);
                cairo_clip(cr);
                cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, 11);
                cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
                double ly = y + 13;
                size_t pos = 0;
                while (pos < text.size() && ly < y + h + 12) {
                    size_t nl = text.find('\n', pos);
                    std::string line = text.substr(pos, nl == TStr::npos ? TStr::npos : nl - pos);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    cairo_move_to(cr, x + 4, ly);
                    cairo_show_text(cr, line.c_str());
                    ly += 13;
                    pos = nl == TStr::npos ? text.size() : nl + 1;
                }
                cairo_restore(cr);
            } else if (k == "Image") {
                box(c.objs->count(AIMPUI_IMAGE_PROPID_IMAGE) ? 0.55 : 0.85, 0.6, 0.7);
            } else if (k == "Paint") {
                cairo_save(cr);
                cairo_translate(cr, x, y);
                RECT r = {0, 0, (INT32)w, (INT32)h};
                c.paint(cr, r);
                cairo_restore(cr);
            }
            if (disabled) {   // greyed out
                cairo_set_source_rgba(cr, 0.97, 0.97, 0.97, 0.55);
                cairo_rectangle(cr, x, y, w, h);
                cairo_fill(cr);
            }
        }
        cairo_destroy(cr);
        char name[64];
        snprintf(name, sizeof name, "/layout-%d.png", ++page);
        cairo_surface_write_to_png(surface, (dir + name).c_str());
        cairo_surface_destroy(surface);
    }
    printf("layout of %d tabs drawn to %s (%d texts too wide)\n", page, dir.c_str(), g_overflows);
}
#endif

#ifdef _WIN32
// Windows: the plugin shows the Windows file dialog. Like a user, this thread waits for it, types the file name and
// presses "Save" / "Open" (or "Cancel" when 'file' is empty). Returns false if no dialog appeared.
bool AnswerFileDialog(const wchar_t* title, const std::wstring& file, const std::function<void()>& open) {
    std::atomic<bool> seen{false};
    std::thread answer([&] {
        for (int i = 0; i < 300; ++i) {   // up to 30 s (Wine starts its dialog slowly the first time)
            HWND dlg = FindWindowW(L"#32770", title);
            if (dlg && IsWindowVisible(dlg)) {
                seen = true;
                Sleep(300);
                if (file.empty()) {
                    PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
                } else {
                    HWND name = GetDlgItem(dlg, 0x47C);   // cmb13: file name (Explorer style dialog)
                    if (!name) name = GetDlgItem(dlg, 0x480);   // edt1
                    SendMessageW(name, WM_SETTEXT, 0, (LPARAM)file.c_str());
                    PostMessageW(dlg, WM_COMMAND, IDOK, 0);
                }
                return;
            }
            Sleep(100);
        }
    });
    open();   // blocks while the dialog is open
    answer.join();
    return seen;
}
#endif

// Opens the settings page, checks the layout, edits values and saves them (what a user does in Preferences).
int TestSettingsPage(Core* core, bool uiOnly) {
    IAIMPOptionsDialogFrame* frame = core->frame;
    if (!frame) return Fail("no options frame registered");
    IAIMPString* name = nullptr;
    if (frame->GetName(&name) != S_OK || !name) return Fail("GetName");
    if (mockui::StrOf(name) != T("Discord Rich Presence")) return Fail("frame name");
    name->Release();

    if (!frame->CreateFrame((HWND)0)) return Fail("CreateFrame returned no window");
    frame->Notification(AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOAD);
    printf("settings page: %d controls\n", (int)core->ui.reg.all.size());
    if (core->ui.reg.all.size() < 100) return Fail("too few controls on the settings page");
    if (!AllTranslated(core)) return Fail("a text was not translated");

    // every control with fixed bounds must be anchored top-left and fit the page with a right margin
    // (AIMP's page is about 478 x 420 at 96 DPI; the controls end at 465 like the 10 px margin on the left)
    int placedCount = 0;
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (!i.placed || i.alignment != 0 /* ualNone */) continue;
        ++placedCount;
        if (i.anchors.left != 1 || i.anchors.top != 1) return Fail("control without top-left anchors");
        if (i.bounds.right <= i.bounds.left || i.bounds.bottom <= i.bounds.top) return Fail("control with empty bounds");
        if (i.bounds.right > 465 || i.bounds.bottom > 420) return Fail("control outside the page / in the right margin");
    }
    if (placedCount < 90) return Fail("too few placed controls");

    // language: AIMP_TEST_LANGUAGE -> the tab names
    const std::string lang = Env("AIMP_TEST_LANGUAGE", "English");
    Info tab;
    const char* general = lang == "Русский" ? "Общие" : lang == "Deutsch" ? "Allgemein" : lang == "Українська" ? "Загальні" : "General";
    if (!FindControl(core, "S", AIMPUI_TABSHEET_PROPID_CAPTION, S(general), tab)) return Fail("tab names not in AIMP's language");
    printf("settings page: language follows AIMP (%s -> \"%s\")\n", lang.c_str(), general);
    // the changelog on the About tab: in the plugin's language, the installed version on top
    {
        const char* installed = lang == "Русский" ? "  (установлена)" : lang == "Deutsch" ? "  (installiert)"
                              : lang == "Українська" ? "  (встановлена)" : "  (installed)";
        const char* section = lang == "Русский" ? "Новое:" : lang == "Deutsch" ? "Neu:" : lang == "Українська" ? "Нове:" : "New:";
        Info changelog;
        if (!FindKind(core, "Memo", changelog, 1)) return Fail("changelog memo");
        const TStr text = (*changelog.strs)[AIMPUI_BASEEDIT_PROPID_TEXT];
        const size_t top = text.find(FromUtf8(AIMP_DISCORD_RPC_VERSION) + S(installed));
        if (top == TStr::npos || top > 400) return Fail("changelog with the installed version on top");
        const bool aiNote = text.find(lang == "Русский" ? S("ИИ") : S("ШІ")) != TStr::npos;
        if ((lang == "Русский" || lang == "Українська") != aiNote) return Fail("AI translation note (only ru / uk)");
        if (text.find(S(section)) == TStr::npos) return Fail("changelog not in the plugin's language");
        printf("settings page: changelog in the plugin's language, %s on top\n", AIMP_DISCORD_RPC_VERSION);
    }
    // the application in use is shown right away, also before the first presence round (default: built-in "AIMP")
    if (!ContainsLabel(core, T("1555109720807702559"))) return Fail("application ID not shown on the Advanced tab");

    const std::string out = Env("AIMP_TEST_OUT", ".");
    if (!RenderPreview(core, out + "/preview-example" kImageExt, 1)) return Fail("preview not drawn (example)");
#ifndef _WIN32
    RenderLayout(core, out);
    if (g_overflows) return Fail("texts too wide for their control (see above)");
#endif
    if (uiOnly) return 0;

    if (!ContainsLabel(core, FromUtf8(AIMP_DISCORD_RPC_VERSION))) return Fail("version label");

    Info details;
    if (!FindControl(core, "Edit", AIMPUI_BASEEDIT_PROPID_TEXT, T("%title%"), details)) return Fail("details edit (%title%)");

    // own application: the ID row is hidden until the box is ticked
    Info custom;
    if (!FindControl(core, "Check", AIMPUI_CHECKBOX_PROPID_CAPTION, T("Use my own Discord application"), custom))
        return Fail("custom app check box");
    Info idLabel;
    if (!FindControl(core, "Label", AIMPUI_LABEL_PROPID_TEXT, T("Application ID:"), idLabel)) return Fail("app id label");
    if ((*idLabel.ints)[AIMPUI_CONTROL_PROPID_VISIBLE] != 0) return Fail("app id row should be hidden");
    if (core->options.modified != 0) return Fail("Load must not mark the page modified");
    (*custom.ints)[AIMPUI_CHECKBOX_PROPID_STATE] = AIMPUI_CHECKSTATE_CHECKED;
    custom.fire();
    if ((*idLabel.ints)[AIMPUI_CONTROL_PROPID_VISIBLE] != 1) return Fail("app id row should be shown");
    (*custom.ints)[AIMPUI_CHECKBOX_PROPID_STATE] = AIMPUI_CHECKSTATE_UNCHECKED;
    custom.fire();

    // export the settings, change a value, import them again
    const Str exported = FromUtf8((out + "/exported-settings.ini").c_str());
    Info exportBtn, importBtn;
    if (!FindControl(core, "Button", AIMPUI_BUTTON_PROPID_CAPTION, T("Export…"), exportBtn) ||
        !FindControl(core, "Button", AIMPUI_BUTTON_PROPID_CAPTION, T("Import…"), importBtn))
        return Fail("export / import buttons");
    (*details.strs)[AIMPUI_BASEEDIT_PROPID_TEXT] = T("%title% [exported]");
#ifdef _WIN32   // the Windows dialog, answered like a user would
    std::wstring winPath = exported;   // typed into the dialog: a Windows path (Wine: Z: is the Linux root)
    if (!winPath.empty() && winPath[0] == L'/') {
        std::replace(winPath.begin(), winPath.end(), L'/', L'\\');
        winPath = L"Z:" + winPath;
    }
    if (!AnswerFileDialog(L"Export settings", winPath, [&] { exportBtn.fire(); })) return Fail("no Windows save dialog");
    if (!ContainsLabel(core, T("Saved: "))) return Fail("export feedback");
    (*details.strs)[AIMPUI_BASEEDIT_PROPID_TEXT] = T("something else");
    if (!AnswerFileDialog(L"Import settings", winPath, [&] { importBtn.fire(); })) return Fail("no Windows open dialog");
    if (!ContainsLabel(core, T("Imported: "))) return Fail("import feedback");
    if ((*details.strs)[AIMPUI_BASEEDIT_PROPID_TEXT] != T("%title% [exported]")) return Fail("import did not restore the value");
    if (!AnswerFileDialog(L"Export settings", std::wstring(), [&] { exportBtn.fire(); })) return Fail("no dialog (cancel)");
    if (core->ui.dialogs.opened != 0) return Fail("AIMP's dialog service used on Windows");
#else           // AIMP's dialog service (mock)
    core->ui.dialogs.pick = exported;
    exportBtn.fire();
    if (!ContainsLabel(core, T("Saved: "))) return Fail("export feedback");
    (*details.strs)[AIMPUI_BASEEDIT_PROPID_TEXT] = T("something else");
    importBtn.fire();
    if (!ContainsLabel(core, T("Imported: "))) return Fail("import feedback");
    if ((*details.strs)[AIMPUI_BASEEDIT_PROPID_TEXT] != T("%title% [exported]")) return Fail("import did not restore the value");
    core->ui.dialogs.pick.clear();
    exportBtn.fire();   // cancelled dialog: nothing happens
#endif
    printf("settings page: export / import OK\n");

    // the user edits the first line
    (*details.strs)[AIMPUI_BASEEDIT_PROPID_TEXT] = T("%title% [test]");
    details.fire();
    if (core->options.modified == 0) return Fail("editing did not enable Apply (FrameModified)");

    // "Clear cover cache" button
    Info clear;
    if (!FindControl(core, "Button", AIMPUI_BUTTON_PROPID_CAPTION, T("Clear cover cache"), clear)) return Fail("clear cache button");
    clear.fire();
    if (!ContainsLabel(core, T("Cover cache cleared."))) return Fail("clear cache feedback");

    frame->Notification(AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_SAVE);
    printf("settings page: OK (Apply enabled %d time(s), saved)\n", core->options.modified);
    return 0;
}

// the edit that was created right after the label with this text (label + edit form one row)
bool EditAfterLabel(Core* core, const TStr& label, Info& out) {
    bool seen = false;
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (seen && i.kind == mockui::Service::K("Edit")) { out = i; return true; }
        auto it = i.strs->find(AIMPUI_LABEL_PROPID_TEXT);
        if (i.kind == mockui::Service::K("Label") && it != i.strs->end() && it->second == label) seen = true;
    }
    return false;
}

bool SetPlaylistFilter(Core* core, const TStr& value) {
    Info e;
    if (!EditAfterLabel(core, T("Hide in playlists containing (;):"), e)) return false;
    (*e.strs)[AIMPUI_BASEEDIT_PROPID_TEXT] = value;
    e.fire();
    core->frame->Notification(AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_SAVE);   // "Apply"
    return true;
}

// pictures still referenced by controls (the mock form is never destroyed); everything else must be released
int PicturesHeldByControls(Core* core) {
    std::vector<IUnknown*> seen;
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        for (auto& kv : *i.objs)
            if (std::find(seen.begin(), seen.end(), kv.second) == seen.end()) seen.push_back(kv.second);
    }
    return (int)seen.size();
}

bool Click(Core* core, const TStr& caption) {
    Info b;
    if (!FindControl(core, "Button", AIMPUI_BUTTON_PROPID_CAPTION, caption, b)) return false;
    b.fire();
    return true;
}

bool HasPicture(Core* core, int nth) {
    Info img;
    return FindKind(core, "Image", img, nth) && img.objs->count(AIMPUI_IMAGE_PROPID_IMAGE) &&
           (*img.ints)[AIMPUI_CONTROL_PROPID_VISIBLE] == 1;
}

int RunTests(Core* core) {
    const bool uiOnly = Env("AIMP_TEST_UI_ONLY") == "1";
    const bool upload = Env("AIMP_TEST_UPLOAD") == "1";   // the track's folder image goes to (fake) catbox.moe
    if (Env("AIMP_TEST_ROTATE") == "1") {   // texts with variants: Discord gets them one after the other
        core->player->state = 2;
        core->player->pos = 30;
        core->player->dur = 354;
        core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_STATE);
        Tick(core, 13);
        printf("tag reads: %d in 13 s\n", core->player->infoReads);
        // a radio stream changes its title without a new track: shown within a few seconds
        core->player->info->text[AIMP_FILEINFO_PROPID_TITLE] = T("Radio Song");
        Tick(core, 5);
        // next track (AIMP reports it): shown at once
        core->player->info->text[AIMP_FILEINFO_PROPID_TITLE] = T("Next Song");
        core->player->pos = 0;
        core->dispatcher->Fire(AIMP_MSG_EVENT_STREAM_START);
        Tick(core, 3);
        return -1;   // no settings page in this run: just finalize
    }
    if (TestSettingsPage(core, uiOnly || upload)) return 1;
    if (upload) {
        core->player->state = 2;
        core->player->pos = 30;
        core->player->dur = 354;
        core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_STATE);
        TStr source;   // AIMP_TEST_UPLOAD_HOST: where the cover should end up (default catbox.moe)
        const TStr host = FromUtf8(("uploaded to " + Env("AIMP_TEST_UPLOAD_HOST", "catbox.moe")).c_str());
        if (!WaitForLabel(core, host, 20, &source)) return Fail("folder cover was not uploaded where expected");
        printf("upload: %s\n", Narrow(source).c_str());
        Tick(core, 10);   // time for the check whether Discord can show the cover (and its repair)
        return 0;
    }

    if (!uiOnly) {
        printf("-> playing\n");
        core->player->state = 2;   // AIMP_PLAYER_STATE_PLAYING
        core->player->pos = 30;
        core->player->dur = 354;
        core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_STATE);
        Tick(core, 4);

        // status line, cover found online (fake Deezer) with its source, pictures, log
        if (!WaitForLabel(core, T("connected to Discord as Test User"), 5)) return Fail("status line not updated");
        if (!WaitForLabel(core, T("found on Deezer"), 10)) return Fail("cover source not shown");
        // the test INI says the last start was 1.4.1: first start of this version -> "update installed" window
        if (core->message->shown != 1 || core->message->text.find(T("was updated to version")) == TStr::npos ||
            core->message->text.find(T("About")) == TStr::npos)
            return Fail("no \"update installed\" window after the update");
        printf("update: window \"%s\": %s\n", Narrow(core->message->caption).c_str(), Narrow(core->message->text).c_str());
        for (int i = 0; i < 20 && !HasPicture(core, 0); ++i) Tick(core, 1);
        if (!HasPicture(core, 0)) return Fail("cover picture not shown on the Cover tab");
        // the author picture on the About tab: drawn with rounded corners into its paint box
        bool avatar = false;
        for (int i = 0; i < 20 && !avatar; ++i) {
            const int before = g_imageDraws;
            if (!RenderPreview(core, Env("AIMP_TEST_OUT", ".") + "/about-avatar" kImageExt, 2, 1))
                return Fail("no paint box for the author picture");
            avatar = g_imageDraws > before;
            if (!avatar) Tick(core, 1);
        }
        if (!avatar) return Fail("author picture not drawn on the About tab");
        Info log;
        if (!FindKind(core, "Memo", log, 0) || log.lines->empty()) return Fail("log lines on the Advanced tab");
        printf("settings page: status, cover source, pictures and log OK (%d log lines)\n", (int)log.lines->size());
        const std::string out = Env("AIMP_TEST_OUT", ".");
        if (!RenderPreview(core, out + "/preview-playing" kImageExt, 1) ||
            !RenderPreview(core, out + "/preview-playing-2x" kImageExt, 2))
            return Fail("preview not drawn (playing)");
        if (g_imageDraws == 0) return Fail("the preview drew no picture");

        // automatic update check (UpdateFrequency=0 in the test INI): downloaded, checked and opened "in AIMP";
        // once "AIMP" has put the new plugin file in place, the plugin restarts AIMP (once)
        if (!WaitForLabel(core, T("AIMP restarts now"), 30)) return Fail("update was not installed / no restart");
        Tick(core, 3);
        if (core->shutdown->restarts != 1) return Fail("AIMP was not restarted (once) after the update");
        printf("update: package downloaded, opened, installed by \"AIMP\" - AIMP restarted\n");

        printf("-> reconnect\n");
        if (!Click(core, T("Reconnect"))) return Fail("reconnect button");
        if (!ContainsLabel(core, T("Reconnecting"))) return Fail("no note while reconnecting");
        // the note follows the connection: "Connected." once it is back, then it disappears
        if (!WaitForLabel(core, T("Connected."), 15)) return Fail("\"Reconnecting\" stays after the reconnect");
        Tick(core, 6);
        if (ContainsLabel(core, T("Connected.")) || ContainsLabel(core, T("Reconnecting")))
            return Fail("reconnect note not cleared");
        printf("reconnect: note shows \"Connected.\" and is cleared\n");
        printf("-> playlist filter\n");   // hides the presence while the track comes from "Test Mix"
        if (!SetPlaylistFilter(core, T("other; test mix"))) return Fail("playlist filter edit");
        Tick(core, 3);
        SetPlaylistFilter(core, T(""));
        Tick(core, 3);
        printf("-> test presence\n");
        if (!Click(core, T("Send test presence"))) return Fail("test button");
        Tick(core, 3);

        printf("-> paused\n");
        core->player->state = 1;
        core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_STATE);
        Tick(core, 2);
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { fprintf(stderr, "usage: %s plugin.so|plugin.dll\n", argv[0]); return 2; }
    {   // started by the plugin's update check: "AIMP opens the package"
        std::string arg = argv[1];
        if (arg.size() > 9 && arg.compare(arg.size() - 9, 9, ".aimppack") == 0) {
            FILE* f = fopen(Env("AIMP_TEST_MARKER", "opened.txt").c_str(), "wb");
            if (f) { fputs(arg.c_str(), f); fclose(f); }
            const std::string plugin = Env("AIMP_TEST_INSTALL");
            if (!plugin.empty()) {   // like AIMP: the loaded plugin becomes .old, the new file takes its place
                std::string bytes;
                if (FILE* in = fopen(plugin.c_str(), "rb")) {
                    char buf[65536];
                    for (size_t n; (n = fread(buf, 1, sizeof buf, in)) > 0;) bytes.append(buf, n);
                    fclose(in);
                }
                remove((plugin + ".old").c_str());
                if (bytes.empty() || rename(plugin.c_str(), (plugin + ".old").c_str()) != 0) return 1;
                FILE* out = fopen(plugin.c_str(), "wb");
                if (!out) return 1;
                fwrite(bytes.data(), 1, bytes.size(), out);
                fclose(out);
            }
            return 0;
        }
    }
#ifdef _WIN32
    HMODULE lib = LoadLibraryA(argv[1]);
    if (!lib) { fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    auto getHeader = reinterpret_cast<TAIMPPluginGetHeaderProc>(GetProcAddress(lib, "AIMPPluginGetHeader"));
#else
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    auto getHeader = reinterpret_cast<TAIMPPluginGetHeaderProc>(dlsym(lib, "AIMPPluginGetHeader"));
#endif
    if (!getHeader) { fprintf(stderr, "AIMPPluginGetHeader not exported\n"); return 1; }

    IAIMPPlugin* plugin = nullptr;
    if (getHeader(&plugin) != S_OK || !plugin) { fprintf(stderr, "AIMPPluginGetHeader failed\n"); return 1; }
    printf("plugin: %s by %s\n", Narrow(plugin->InfoGet(AIMP_PLUGIN_INFO_NAME)).c_str(),
           Narrow(plugin->InfoGet(AIMP_PLUGIN_INFO_AUTHOR)).c_str());

    Core* core = g_core = new Core();
    FileInfo* info = new FileInfo();
    info->text[AIMP_FILEINFO_PROPID_ARTIST] = T("Queen");
    info->text[AIMP_FILEINFO_PROPID_TITLE] = S("Bohemian Rhapsody \xE2\x80\x93 Remastered");   // en dash
#ifdef _WIN32
    info->text[AIMP_FILEINFO_PROPID_FILENAME] = L"C:\\Music\\Queen\\01.flac";
#else
    info->text[AIMP_FILEINFO_PROPID_FILENAME] = "/music/Queen/01.flac";
#endif
    if (!Env("AIMP_TEST_TRACK").empty()) info->text[AIMP_FILEINFO_PROPID_FILENAME] = FromUtf8(Env("AIMP_TEST_TRACK").c_str());
    info->text[AIMP_FILEINFO_PROPID_ALBUM] = T("A Night at the Opera");
    info->duration = 354;
    core->player->info = info;
    Playlist* playlist = new Playlist();
    playlist->props->text[AIMP_PLAYLIST_PROPID_NAME] = T("Test Mix");
    PlaylistItem* item = new PlaylistItem();
    item->text[AIMP_PLAYLISTITEM_PROPID_FILENAME] = info->text[AIMP_FILEINFO_PROPID_FILENAME];
    item->objects[AIMP_PLAYLISTITEM_PROPID_PLAYLIST] = static_cast<IAIMPPlaylist*>(playlist);
    core->player->item = item;

    if (plugin->Initialize(core) != S_OK) { fprintf(stderr, "Initialize failed\n"); return 1; }
    if (!core->dispatcher->hook) { fprintf(stderr, "plugin did not hook AIMP's messages\n"); return 1; }

    const int rc = RunTests(core);
    if (rc) {
        plugin->Finalize();   // stop the plugin's threads before leaving
        return rc < 0 ? 0 : rc;
    }

    core->frame->DestroyFrame();
    if (!core->ui.lastForm || !core->ui.lastForm->destroyed) return Fail("DestroyFrame did not destroy the form");

    printf("-> finalize\n");
    plugin->Finalize();
    if (core->dispatcher->hook) { fprintf(stderr, "plugin did not unhook\n"); return 1; }
    plugin->Release();
#ifdef _WIN32
    FreeLibrary(lib);
#else
    dlclose(lib);
#endif
    const int held = PicturesHeldByControls(core);
    printf("images alive: %d (held by controls: %d)\n", g_images.load(), held);
    if (g_images != held) return Fail("image objects leaked");
    printf("done\n");
    return 0;
}
