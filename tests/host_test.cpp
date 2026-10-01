// Mock AIMP host: loads the plugin the way AIMP does, plays a fake track through the player service and lets
// the plugin talk to tests/fake_discord.py.
//   Linux:   host_test aimp_discord_rpc.so    (events through the message hook)
//   Windows: host_test.exe aimp_discord_rpc.dll (timer window; run under Wine against the Linux fake socket)
// Environment: XDG_RUNTIME_DIR (fake Discord socket) and XDG_CONFIG_HOME / APPDATA (settings) point to temp dirs.

#ifdef _WIN32
#include <windows.h>
#endif

#include "apiCore.h"
#include "apiFileManager.h"
#include "apiMessages.h"
#include "apiPlayer.h"
#include "apiPlaylists.h"
#include "apiPlugin.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#ifdef _WIN32
#define T(s) L##s
using Str = std::wstring;
#else
#define T(s) s
using Str = std::string;
#endif

namespace {

bool Same(REFIID a, REFIID b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }

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

class FileInfo : public Obj<IAIMPFileInfo> {
public:
    std::map<int, Str> text;
    double duration = 0;
    void WINAPI BeginUpdate() override {}
    void WINAPI EndUpdate() override {}
    HRESULT WINAPI Reset() override { return S_OK; }
    HRESULT WINAPI GetValueAsFloat(INT32 id, DOUBLE* v) override {
        if (id != AIMP_FILEINFO_PROPID_DURATION) return E_FAIL;
        *v = duration;
        return S_OK;
    }
    HRESULT WINAPI GetValueAsInt32(INT32, INT32*) override { return E_FAIL; }
    HRESULT WINAPI GetValueAsInt64(INT32, INT64*) override { return E_FAIL; }
    HRESULT WINAPI GetValueAsObject(INT32 id, CONSTIID iid, void** v) override {
        auto it = text.find(id);
        if (it == text.end() || !Same(iid, IID_IAIMPString)) return E_FAIL;
        String* s = new String();
        s->s = it->second;
        *v = static_cast<IAIMPString*>(s);
        return S_OK;
    }
    HRESULT WINAPI SetValueAsFloat(INT32, const DOUBLE) override { return E_NOTIMPL; }
    HRESULT WINAPI SetValueAsInt32(INT32, INT32) override { return E_NOTIMPL; }
    HRESULT WINAPI SetValueAsInt64(INT32, const INT64) override { return E_NOTIMPL; }
    HRESULT WINAPI SetValueAsObject(INT32, IUnknown*) override { return E_NOTIMPL; }
    HRESULT WINAPI Assign(IAIMPFileInfo*) override { return E_NOTIMPL; }
    HRESULT WINAPI Clone(IAIMPFileInfo**) override { return E_NOTIMPL; }
};

class Player : public Obj<IAIMPServicePlayer> {
public:
    int state = 0;
    double pos = 0, dur = 0;
    FileInfo* info = nullptr;
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
        if (!info) return E_FAIL;
        info->AddRef();
        *fi = info;
        return S_OK;
    }
    HRESULT WINAPI GetPlaylistItem(IAIMPPlaylistItem**) override { return E_FAIL; }
    INT32 WINAPI GetState() override { return state; }
    HRESULT WINAPI Pause() override { return E_NOTIMPL; }
    HRESULT WINAPI Resume() override { return E_NOTIMPL; }
    HRESULT WINAPI Stop() override { return E_NOTIMPL; }
    HRESULT WINAPI StopAfterTrack() override { return E_NOTIMPL; }
};

class Dispatcher : public Obj<IAIMPServiceMessageDispatcher> {
public:
    IAIMPMessageHook* hook = nullptr;
    HRESULT WINAPI Send(DWORD, INT32, void*) override { return E_NOTIMPL; }
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

class Core : public Obj<IAIMPCore> {
public:
    Player* player = new Player();
    Dispatcher* dispatcher = new Dispatcher();
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPServicePlayer)) { player->AddRef(); *ppv = player; return S_OK; }
        if (Same(riid, IID_IAIMPServiceMessageDispatcher)) { dispatcher->AddRef(); *ppv = dispatcher; return S_OK; }
        return Obj<IAIMPCore>::QueryInterface(riid, ppv);
    }
    HRESULT WINAPI CreateObject(CONSTIID iid, void** obj) override {
        if (!Same(iid, IID_IAIMPString)) return E_NOTIMPL;
        *obj = static_cast<IAIMPString*>(new String());
        return S_OK;
    }
    HRESULT WINAPI GetPath(int, IAIMPString**) override { return E_NOTIMPL; }
    HRESULT WINAPI RegisterExtension(CONSTIID, IUnknown*) override { return S_OK; }
    HRESULT WINAPI RegisterService(IUnknown*) override { return E_NOTIMPL; }
    HRESULT WINAPI UnregisterExtension(IUnknown*) override { return S_OK; }
};

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
    usleep(500 * 1000);
#endif
}

void Tick(Core* core, int seconds) {   // AIMP fires UPDATE_POSITION once per second while playing
    for (int i = 0; i < seconds * 2; ++i) {
        Wait500ms();
        if (core->player->state == 2) core->player->pos += 0.5;
        core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_UPDATE_POSITION);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s plugin.so|plugin.dll\n", argv[0]); return 2; }
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
#ifdef _WIN32
    printf("plugin: %ls by %ls\n", plugin->InfoGet(AIMP_PLUGIN_INFO_NAME), plugin->InfoGet(AIMP_PLUGIN_INFO_AUTHOR));
#else
    printf("plugin: %s by %s\n", plugin->InfoGet(AIMP_PLUGIN_INFO_NAME), plugin->InfoGet(AIMP_PLUGIN_INFO_AUTHOR));
#endif

    Core* core = new Core();
    FileInfo* info = new FileInfo();
    info->text[AIMP_FILEINFO_PROPID_ARTIST] = T("Queen");
#ifdef _WIN32
    info->text[AIMP_FILEINFO_PROPID_TITLE] = L"Bohemian Rhapsody \u2013 Remastered";
    info->text[AIMP_FILEINFO_PROPID_FILENAME] = L"C:\\Music\\Queen\\01.flac";
#else
    info->text[AIMP_FILEINFO_PROPID_TITLE] = "Bohemian Rhapsody \xE2\x80\x93 Remastered";   // UTF-8 en dash
    info->text[AIMP_FILEINFO_PROPID_FILENAME] = "/music/Queen/01.flac";
#endif
    info->text[AIMP_FILEINFO_PROPID_ALBUM] = T("A Night at the Opera");
    info->duration = 354;
    core->player->info = info;

    if (plugin->Initialize(core) != S_OK) { fprintf(stderr, "Initialize failed\n"); return 1; }
#ifndef _WIN32
    if (!core->dispatcher->hook) { fprintf(stderr, "plugin did not hook player events\n"); return 1; }
#endif

    printf("-> playing\n");
    core->player->state = 2;   // AIMP_PLAYER_STATE_PLAYING
    core->player->pos = 30;
    core->player->dur = 354;
    core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_STATE);
    Tick(core, 4);

    printf("-> paused\n");
    core->player->state = 1;
    core->dispatcher->Fire(AIMP_MSG_EVENT_PLAYER_STATE);
    Tick(core, 3);

    printf("-> finalize\n");
    plugin->Finalize();
    if (core->dispatcher->hook) { fprintf(stderr, "plugin did not unhook\n"); return 1; }
    plugin->Release();
#ifdef _WIN32
    FreeLibrary(lib);
#else
    dlclose(lib);
#endif
    printf("done\n");
    return 0;
}
