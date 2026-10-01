// Mock AIMP host: loads the plugin the way AIMP does, plays a fake track through the player service and lets
// the plugin talk to tests/fake_discord.py.
//   Linux:   host_test aimp_discord_rpc.so    (events through the message hook)
//   Windows: host_test.exe aimp_discord_rpc.dll (timer window; run under Wine against the Linux fake socket)
// It also opens the plugin's settings page through a mock of AIMP's UI service (tests/mock_ui.h), changes a value
// like a user would and checks that "Apply" is enabled, the value is saved and used for the presence.
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
#include "apiOptions.h"
#include "apiGUI.h"

#include "../src/version.h"

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
const wchar_t* const kVersion = AIMP_DISCORD_RPC_VERSION_W;
using Str = std::wstring;
#else
#define T(s) s
const char* const kVersion = AIMP_DISCORD_RPC_VERSION;
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

}  // namespace

namespace mockui {
IAIMPString* NewString(const std::basic_string<TChar>& s) {
    String* r = new String();
    r->s = s;
    return r;
}
}  // namespace mockui
#include "mock_ui.h"

namespace {

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
    mockui::Service ui;
    mockui::OptionsService options;
    IAIMPOptionsDialogFrame* frame = nullptr;
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (Same(riid, IID_IAIMPServicePlayer)) { player->AddRef(); *ppv = player; return S_OK; }
        if (Same(riid, IID_IAIMPServiceMessageDispatcher)) { dispatcher->AddRef(); *ppv = dispatcher; return S_OK; }
        if (Same(riid, IID_IAIMPServiceUI)) { *ppv = static_cast<IAIMPServiceUI*>(&ui); return S_OK; }
        if (Same(riid, IID_IAIMPServiceOptionsDialog)) { *ppv = static_cast<IAIMPServiceOptionsDialog*>(&options); return S_OK; }
        return Obj<IAIMPCore>::QueryInterface(riid, ppv);
    }
    HRESULT WINAPI CreateObject(CONSTIID iid, void** obj) override {
        if (!Same(iid, IID_IAIMPString)) return E_NOTIMPL;
        *obj = static_cast<IAIMPString*>(new String());
        return S_OK;
    }
    HRESULT WINAPI GetPath(int, IAIMPString**) override { return E_NOTIMPL; }
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

int Fail(const char* what) {
    fprintf(stderr, "FAILED: %s\n", what);
    return 1;
}

using Info = mockui::Registry::Info;

// first control of the given kind whose property 'prop' equals 'text'
bool FindControl(Core* core, const char* kind, int prop, const mockui::TStr& text, Info& out) {
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (i.kind != mockui::Service::K(kind)) continue;
        auto it = i.strs->find(prop);
        if (it != i.strs->end() && it->second == text) { out = i; return true; }
    }
    return false;
}

bool ContainsLabel(Core* core, const mockui::TStr& part, mockui::TStr* full = nullptr) {
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (i.kind != mockui::Service::K("Label")) continue;
        auto it = i.strs->find(AIMPUI_LABEL_PROPID_TEXT);
        if (it != i.strs->end() && it->second.find(part) != mockui::TStr::npos) {
            if (full) *full = it->second;
            return true;
        }
    }
    return false;
}

// Opens the settings page, edits the details line and saves it (what a user does in Preferences).
int TestSettingsPage(Core* core) {
    IAIMPOptionsDialogFrame* frame = core->frame;
    if (!frame) return Fail("no options frame registered");
    IAIMPString* name = nullptr;
    if (frame->GetName(&name) != S_OK || !name) return Fail("GetName");
    if (mockui::StrOf(name) != T("Discord Rich Presence")) return Fail("frame name");
    name->Release();

    if (!frame->CreateFrame((HWND)0)) return Fail("CreateFrame returned no window");
    frame->Notification(AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOAD);
    printf("settings page: %d controls\n", (int)core->ui.reg.all.size());
    if (core->ui.reg.all.size() < 60) return Fail("too few controls on the settings page");
    if (!ContainsLabel(core, kVersion)) return Fail("version label");

    // every control with fixed bounds must be anchored top-left, otherwise AIMP moves it when the page grows
    int placedCount = 0;
    for (auto& get : core->ui.reg.all) {
        Info i = get();
        if (!i.placed || i.alignment != 0 /* ualNone */) continue;
        ++placedCount;
        if (i.anchors.left != 1 || i.anchors.top != 1) return Fail("control without top-left anchors");
        if (i.bounds.right <= i.bounds.left || i.bounds.bottom <= i.bounds.top) return Fail("control with empty bounds");
        if (i.bounds.right > 480) return Fail("control wider than the page");
    }
    if (placedCount < 60) return Fail("too few placed controls");

    Info details;
    if (!FindControl(core, "Edit", AIMPUI_BASEEDIT_PROPID_TEXT, T("%title%"), details)) return Fail("details edit (%title%)");

    // advanced app id row is hidden until the box is ticked
    Info custom;
    if (!FindControl(core, "Check", AIMPUI_CHECKBOX_PROPID_CAPTION, T("Advanced: use my own Discord application"), custom))
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

    if (TestSettingsPage(core)) return 1;

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

    {   // the status line on the open settings page follows the connection
        mockui::TStr status;
        if (!ContainsLabel(core, T("Status: "), &status)) return Fail("status label");
        if (status.find(T("connected to Discord as tester")) == mockui::TStr::npos) return Fail("status line not updated");
        printf("settings page status line updated\n");
        core->frame->DestroyFrame();
        if (!core->ui.lastForm || !core->ui.lastForm->destroyed) return Fail("DestroyFrame did not destroy the form");
    }

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
