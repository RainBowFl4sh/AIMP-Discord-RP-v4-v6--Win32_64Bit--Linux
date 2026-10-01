// AIMP SDK glue: plugin entry point, options-dialog frame and player polling.
// This is the ONLY file that includes AIMP SDK headers. If your SDK revision names something differently,
// the fix is local to this file (see README -> "SDK notes").

#define INITGUID            // define the SDK's IID_* GUIDs in this translation unit
#include <windows.h>

#include "apiCore.h"
#include "apiFileManager.h"
#include "apiMessages.h"
#include "apiObjects.h"
#include "apiOptions.h"
#include "apiPlayer.h"
#include "apiPlaylists.h"
#include "apiPlugin.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>

#include "config.h"
#include "cover.h"
#include "presence.h"
#include "settings_ui.h"
#include "track.h"
#include "util.h"

HINSTANCE g_hModule = nullptr;

namespace {

const wchar_t* kPluginName = L"Discord Rich Presence";
const int kPlayerStopped = 0, kPlayerPaused = 1, kPlayerPlaying = 2;   // AIMP_MSG_PROPERTY_PLAYER_STATE values

// ---- minimal COM smart pointer ------------------------------------------------------------------------
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { reset(); }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T** put() { reset(); return &p_; }
    void** putVoid() { reset(); return reinterpret_cast<void**>(&p_); }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
private:
    T* p_ = nullptr;
};

std::wstring StringOf(IAIMPString* s) {
    if (!s) return std::wstring();
    PWCHAR d = s->GetData();
    int n = s->GetLength();
    return (d && n > 0) ? std::wstring(d, (size_t)n) : std::wstring();
}

std::wstring PropString(IAIMPPropertyList* pl, int id) {
    ComPtr<IAIMPString> s;
    if (SUCCEEDED(pl->GetValueAsObject(id, IID_IAIMPString, s.putVoid())) && s) return StringOf(s.get());
    return std::wstring();
}

class Plugin;
Plugin* g_plugin = nullptr;

// ---- options dialog frame -----------------------------------------------------------------------------
class OptionsFrame : public IAIMPOptionsDialogFrame {
public:
    explicit OptionsFrame(Plugin* owner) : owner_(owner) {}

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IAIMPOptionsDialogFrame) {
            *ppv = static_cast<IAIMPOptionsDialogFrame*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    ULONG WINAPI Release() override {
        ULONG r = (ULONG)InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // IAIMPOptionsDialogFrame
    HRESULT WINAPI GetName(IAIMPString** S) override;
    HWND WINAPI CreateFrame(HWND parent) override;
    void WINAPI DestroyFrame() override;
    void WINAPI Notification(int id) override;

    void SetService(IAIMPServiceOptionsDialog* svc) { service_ = svc; }

private:
    Plugin* owner_;
    SettingsPage* page_ = nullptr;
    IAIMPServiceOptionsDialog* service_ = nullptr;   // not owned
    volatile LONG ref_ = 1;
};

// ---- the plugin ---------------------------------------------------------------------------------------
class Plugin : public IAIMPPlugin {
public:
    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown) {
            *ppv = static_cast<IAIMPPlugin*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    ULONG WINAPI Release() override {
        ULONG r = (ULONG)InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // IAIMPPlugin
    PWCHAR WINAPI InfoGet(int index) override {
        switch (index) {
            case AIMP_PLUGIN_INFO_NAME:              return const_cast<PWCHAR>(kPluginName);
            case AIMP_PLUGIN_INFO_AUTHOR:            return const_cast<PWCHAR>(L"Community");
            case AIMP_PLUGIN_INFO_SHORT_DESCRIPTION: return const_cast<PWCHAR>(L"Discord Rich Presence with progress bar and cover art");
            default:                                 return nullptr;
        }
    }
    DWORD WINAPI InfoGetCategories() override { return AIMP_PLUGIN_CATEGORY_ADDONS; }
    HRESULT WINAPI Initialize(IAIMPCore* core) override;
    HRESULT WINAPI Finalize() override;
    void WINAPI SystemNotification(int, IUnknown*) override {}

    IAIMPCore* Core() const { return core_; }
    void OnSettingsChanged() { forcePush_ = true; }

    void Poll();

private:
    static LRESULT CALLBACK TimerWndProc(HWND, UINT, WPARAM, LPARAM);
    double ReadReal(int message);
    int ReadState();
    bool ReadTrack(TrackInfo& t, double& duration);

    IAIMPCore* core_ = nullptr;
    ComPtr<IAIMPServicePlayer> player_;
    ComPtr<IAIMPServiceMessageDispatcher> dispatcher_;
    ComPtr<IAIMPServiceOptionsDialog> options_;
    OptionsFrame* frame_ = nullptr;
    HWND timerWnd_ = nullptr;
    volatile LONG ref_ = 1;

    // polling state (main thread only)
    PlayState lastState_ = PlayState::Stopped;
    uint64_t lastId_ = 0;
    double lastPos_ = 0;
    std::chrono::steady_clock::time_point lastPoll_ = std::chrono::steady_clock::now();
    bool forcePush_ = true;
};

// ------------------------------------------------------------------------------------------------ frame impl

HRESULT WINAPI OptionsFrame::GetName(IAIMPString** S) {
    IAIMPCore* core = owner_->Core();
    if (!core || !S) return E_FAIL;
    IAIMPString* str = nullptr;
    if (FAILED(core->CreateObject(IID_IAIMPString, reinterpret_cast<void**>(&str))) || !str) return E_FAIL;
    str->SetData(const_cast<PWCHAR>(kPluginName), (int)wcslen(kPluginName));
    *S = str;
    return S_OK;
}

HWND WINAPI OptionsFrame::CreateFrame(HWND parent) {
    page_ = SettingsPage::Create(parent, [this]() {
        if (service_) service_->FrameModified(this);   // enables AIMP's "Apply" button
    });
    return page_ ? page_->Hwnd() : nullptr;
}

void WINAPI OptionsFrame::DestroyFrame() {
    if (page_) {
        page_->Destroy();
        page_ = nullptr;
    }
}

void WINAPI OptionsFrame::Notification(int id) {
    switch (id) {
        case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOAD:
            if (page_) page_->Load();
            break;
        case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_SAVE:
            if (page_) {
                page_->Save();
                if (g_plugin) g_plugin->OnSettingsChanged();
            }
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------------------------------------------ plugin impl

HRESULT WINAPI Plugin::Initialize(IAIMPCore* core) {
    core_ = core;
    core_->AddRef();
    g_plugin = this;

    core_->QueryInterface(IID_IAIMPServicePlayer, player_.putVoid());
    core_->QueryInterface(IID_IAIMPServiceMessageDispatcher, dispatcher_.putVoid());
    if (!player_ || !dispatcher_) return E_FAIL;

    config::Load();
    Worker().Start();

    // settings tab inside AIMP's options dialog
    frame_ = new OptionsFrame(this);
    if (SUCCEEDED(core_->QueryInterface(IID_IAIMPServiceOptionsDialog, options_.putVoid())) && options_)
        frame_->SetService(options_.get());
    core_->RegisterExtension(IID_IAIMPServiceOptionsDialog, frame_);

    // hidden message-only window: its WM_TIMER runs on AIMP's main thread, so all SDK calls stay there
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &Plugin::TimerWndProc;
    wc.hInstance = g_hModule;
    wc.lpszClassName = L"AIMPDiscordRPCTimerWindow";
    RegisterClassW(&wc);
    timerWnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hModule, nullptr);
    if (timerWnd_) {
        SetWindowLongPtrW(timerWnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetTimer(timerWnd_, 1, 500, nullptr);
    }
    return S_OK;
}

HRESULT WINAPI Plugin::Finalize() {
    if (timerWnd_) {
        KillTimer(timerWnd_, 1);
        DestroyWindow(timerWnd_);
        timerWnd_ = nullptr;
    }
    UnregisterClassW(L"AIMPDiscordRPCTimerWindow", g_hModule);

    if (core_ && frame_) {
        core_->UnregisterExtension(frame_);
        frame_->SetService(nullptr);
        frame_->Release();
        frame_ = nullptr;
    }
    Worker().Stop();               // clears the presence and joins the thread
    CoverResolver::ShutdownImaging();

    options_.reset();
    player_.reset();
    dispatcher_.reset();
    if (core_) {
        core_->Release();
        core_ = nullptr;
    }
    g_plugin = nullptr;
    return S_OK;
}

LRESULT CALLBACK Plugin::TimerWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) {
        Plugin* self = reinterpret_cast<Plugin*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self) self->Poll();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// The player reports position / duration either as float or double depending on SDK revision.
// Read into a zeroed 8-byte buffer: memory-safe in both cases, then decode.
double Plugin::ReadReal(int message) {
    alignas(8) uint8_t buf[8] = {0};
    if (FAILED(dispatcher_->Send(message, AIMP_MSG_PROPVALUE_GET, buf))) return 0.0;
    uint32_t hi;
    memcpy(&hi, buf + 4, 4);
    double v;
    if (hi == 0) {
        float f;
        memcpy(&f, buf, 4);
        v = f;
    } else {
        memcpy(&v, buf, 8);
    }
    return (std::isfinite(v) && v > 0) ? v : 0.0;
}

int Plugin::ReadState() {
    alignas(8) uint8_t buf[8] = {0};
    if (FAILED(dispatcher_->Send(AIMP_MSG_PROPERTY_PLAYER_STATE, AIMP_MSG_PROPVALUE_GET, buf))) return kPlayerStopped;
    int32_t v;
    memcpy(&v, buf, 4);
    return v;
}

bool Plugin::ReadTrack(TrackInfo& t, double& duration) {
    ComPtr<IAIMPPlaylistItem> item;
    if (FAILED(player_->GetPlaylistItem(item.put())) || !item) return false;

    t.fileName = PropString(item.get(), AIMP_PLAYLISTITEM_PROPID_FILENAME);

    ComPtr<IAIMPFileInfo> info;
    if (SUCCEEDED(item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, info.putVoid())) && info) {
        t.artist      = PropString(info.get(), AIMP_FILEINFO_PROPID_ARTIST);
        t.albumArtist = PropString(info.get(), AIMP_FILEINFO_PROPID_ALBUMARTIST);
        t.title       = PropString(info.get(), AIMP_FILEINFO_PROPID_TITLE);
        t.album       = PropString(info.get(), AIMP_FILEINFO_PROPID_ALBUM);
        t.genre       = PropString(info.get(), AIMP_FILEINFO_PROPID_GENRE);
        t.year        = PropString(info.get(), AIMP_FILEINFO_PROPID_DATE);
        t.trackNumber = PropString(info.get(), AIMP_FILEINFO_PROPID_TRACKNUMBER);
        double d = 0;
        if (SUCCEEDED(info->GetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION, &d)) && d > 0) duration = d;
    }
    if (t.title.empty()) t.title = util::FileNameNoExt(t.fileName);   // untagged files / streams
    return true;
}

void Plugin::Poll() {
    using namespace std::chrono;
    const auto now = steady_clock::now();

    int st = ReadState();
    PlayState ps = (st == kPlayerPlaying) ? PlayState::Playing : (st == kPlayerPaused ? PlayState::Paused : PlayState::Stopped);

    Snapshot s;
    s.state = ps;
    s.stamp = now;
    bool push = forcePush_;

    if (ps == PlayState::Stopped) {
        if (lastState_ != PlayState::Stopped) push = true;
        s.trackId = lastId_;
    } else {
        double pos = ReadReal(AIMP_MSG_PROPERTY_PLAYER_POSITION);
        double dur = 0;
        TrackInfo t;
        if (!ReadTrack(t, dur)) return;
        if (dur <= 0) dur = ReadReal(AIMP_MSG_PROPERTY_PLAYER_DURATION);

        uint64_t id = util::Fnv1a(t.fileName + L"|" + t.artist + L"|" + t.title + L"|" + t.album);
        if (id != lastId_) push = true;                     // new track
        if (ps != lastState_) push = true;                  // play <-> pause
        double expected = lastPos_ + (lastState_ == PlayState::Playing ? duration<double>(now - lastPoll_).count() : 0.0);
        if (std::fabs(pos - expected) > 1.5) push = true;   // user seeked / track restarted

        s.position = pos;
        s.duration = dur;
        s.trackId = id;
        s.track = std::move(t);
        lastId_ = id;
        lastPos_ = pos;
    }

    lastState_ = ps;
    lastPoll_ = now;
    if (push) {
        forcePush_ = false;
        Worker().Submit(s);
    }
}

}  // namespace

// ================================================================================ DLL exports

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI AIMPPluginGetHeader(IAIMPPlugin** header) {
    if (!header) return E_POINTER;
    *header = new Plugin();
    return S_OK;
}
