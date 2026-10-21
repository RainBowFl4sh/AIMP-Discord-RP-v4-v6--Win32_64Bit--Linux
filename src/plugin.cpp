// AIMP SDK glue: plugin entry point, options-dialog frame and player polling.
// AIMP SDK headers are only used here and in settings_ui.cpp / preview.cpp (via aimp_util.h).
//
// Settings tab: built with AIMP's own UI API (settings_ui.cpp), identical on Windows and Linux.
// Windows (AIMP x86 / x64): polling by a timer on the main thread.
// Linux (native AIMP for Linux, x86_64): the SDK uses UTF-8 strings there; polling is driven by AIMP's own
// player events (message hook, main thread). Settings are stored in ~/.config/AIMP/DiscordRPC.ini.

#ifdef _WIN32
#define INITGUID            // define the SDK's IID_* GUIDs in this translation unit
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "aimp_util.h"
#include "apiActions.h"
#include "apiCore.h"
#include "apiFileManager.h"
#include "apiGUI.h"
#include "apiMUI.h"
#include "apiMessages.h"
#include "apiObjects.h"
#include "apiOptions.h"
#include "apiPlayer.h"
#include "apiPlaylists.h"
#include "apiPlugin.h"
#include "apiThreading.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <mutex>

#include "config.h"
#include "cover.h"
#include "i18n.h"
#include "jobs.h"
#include "presence.h"
#include "settings_ui.h"
#include "track.h"
#include "update.h"
#include "util.h"
#include "web.h"

#ifdef _WIN32
HINSTANCE g_hModule = nullptr;
#endif

namespace {

using aimp::ComPtr;
using aimp::PropString;
using aimp::SameIID;

const TChar* const kPluginName = AIMP_TEXT("Discord Rich Presence");

const int kPlayerStopped = 0, kPlayerPaused = 1, kPlayerPlaying = 2;   // AIMP_MSG_PROPERTY_PLAYER_STATE values

class Plugin;
Plugin* g_plugin = nullptr;   // main thread only

// ---- options dialog frame -----------------------------------------------------------------------------
class OptionsFrame final : public IAIMPOptionsDialogFrame {
public:
    explicit OptionsFrame(Plugin* owner) : owner_(owner) {}

    // IUnknown
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown) || SameIID(riid, IID_IAIMPOptionsDialogFrame)) {
            *ppv = static_cast<IAIMPOptionsDialogFrame*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return (DWORD)++ref_; }
    DWORD __unknwncall Release() override {
        DWORD r = (DWORD)--ref_;
        if (r == 0) delete this;
        return r;
    }

    // IAIMPOptionsDialogFrame
    HRESULT WINAPI GetName(IAIMPString** S) override;
    HWND WINAPI CreateFrame(HWND parent) override;
    void WINAPI DestroyFrame() override;
    void WINAPI Notification(INT32 id) override;

    void SetService(IAIMPServiceOptionsDialog* svc) { service_ = svc; }
    void Refresh() { if (page_) page_->Refresh(); }
    void Reload() { if (page_) page_->Load(); }   // the settings changed elsewhere (preset hot key)

private:
    Plugin* owner_;
    SettingsPage* page_ = nullptr;
    IAIMPServiceOptionsDialog* service_ = nullptr;   // not owned
    std::atomic<long> ref_{1};
};

// ---- AIMP messages (main thread): language changes, and on Linux the player events that drive the polling
class EventHook final : public IAIMPMessageHook {
public:
    explicit EventHook(Plugin* owner) : owner_(owner) {}

    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown) || SameIID(riid, IID_IAIMPMessageHook)) {
            *ppv = static_cast<IAIMPMessageHook*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return (DWORD)++ref_; }
    DWORD __unknwncall Release() override {
        DWORD r = (DWORD)--ref_;
        if (r == 0) delete this;
        return r;
    }

    void WINAPI CoreMessage(DWORD message, INT32 param1, void* param2, HRESULT* result) override;

    void Detach() { owner_ = nullptr; }

private:
    Plugin* owner_;
    std::atomic<long> ref_{1};
};

// ---- hot key actions: "load preset N" (0..9), "next preset" (-1) and "Discord display on / off" (-2);
// AIMP lists them under Preferences > Hot keys
class PresetAction final : public IAIMPActionEvent {
public:
    explicit PresetAction(int index) : index_(index) {}

    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown) || SameIID(riid, IID_IAIMPActionEvent)) {
            *ppv = static_cast<IAIMPActionEvent*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return (DWORD)++ref_; }
    DWORD __unknwncall Release() override {
        DWORD r = (DWORD)--ref_;
        if (r == 0) delete this;
        return r;
    }
    void WINAPI OnExecute(IUnknown*) override;

private:
    int index_;
    std::atomic<long> ref_{1};
};

#ifndef _WIN32
// Background threads ask for a settings page refresh through AIMP's thread service (Linux has no timer here).
// One static task: nothing to allocate, and calls are merged while one is pending.
class UiTask final : public IAIMPTask {
public:
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown) || SameIID(riid, IID_IAIMPTask)) {
            *ppv = static_cast<IAIMPTask*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return 2; }   // static object
    DWORD __unknwncall Release() override { return 1; }
    void WINAPI Execute(IAIMPTaskOwner*) override;
};
UiTask g_uiTask;
std::atomic<bool> g_uiPending{false};
std::mutex g_threadsMu;
IAIMPServiceThreads* g_threads = nullptr;
#endif

// ---- the plugin ---------------------------------------------------------------------------------------
class Plugin final : public IAIMPPlugin {
public:
    // IUnknown
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown)) {
            *ppv = static_cast<IAIMPPlugin*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return (DWORD)++ref_; }
    DWORD __unknwncall Release() override {
        DWORD r = (DWORD)--ref_;
        if (r == 0) delete this;
        return r;
    }

    // IAIMPPlugin
    PChar WINAPI InfoGet(INT32 index) override {
        switch (index) {
            case AIMP_PLUGIN_INFO_NAME:              return const_cast<PChar>(kPluginName);
            case AIMP_PLUGIN_INFO_AUTHOR:            return const_cast<PChar>(AIMP_TEXT("Fl4sh"));
            case AIMP_PLUGIN_INFO_SHORT_DESCRIPTION: return const_cast<PChar>(AIMP_TEXT("Discord Rich Presence with progress bar and cover art"));
            default:                                 return nullptr;
        }
    }
    DWORD WINAPI InfoGetCategories() override { return AIMP_PLUGIN_CATEGORY_ADDONS; }
    HRESULT WINAPI Initialize(IAIMPCore* core) override;
    HRESULT WINAPI Finalize() override;
    void WINAPI SystemNotification(INT32, IUnknown*) override {}

    IAIMPCore* Core() const { return core_; }
    void OnSettingsChanged() { forcePush_ = true; }
    void OnTrackEvent() { trackDirty_ = true; }   // AIMP reports a new track / new stream title
    void DetectLanguage();
    void OnUiNotify();
    void RestartAimp();
    void ShowPopup(const std::wstring& title, const std::wstring& text);
    void LoadPresetByKey(int index);   // hot key: preset 0..9, -1 = the next stored one
    void ToggleByKey();                // hot key: Discord display on / off
    void NameActions();                // action names in AIMP's hot key list (language, preset names)

    void Poll();

private:
#ifdef _WIN32
    static LRESULT CALLBACK TimerWndProc(HWND, UINT, WPARAM, LPARAM);
#endif
    void RegisterActions();
    double ReadReal(int message);
    int ReadState();
    bool ReadTrack(TrackInfo& t, double& duration);
    std::wstring ReadNext();   // the track AIMP plays next, "" = none / unknown
    void ReadExtra(IAIMPFileInfo* info, IAIMPPlaylistItem* item, TrackInfo& t);
    std::wstring FormatMacro(IAIMPFileInfo* info, const wchar_t* macro);

    IAIMPCore* core_ = nullptr;
    ComPtr<IAIMPServicePlayer> player_;
    ComPtr<IAIMPServiceMessageDispatcher> dispatcher_;
    ComPtr<IAIMPServiceOptionsDialog> options_;
    OptionsFrame* frame_ = nullptr;
    EventHook* hook_ = nullptr;
    std::vector<IAIMPAction*> actions_;   // registered hot key actions (one reference each)
#ifdef _WIN32
    HWND timerWnd_ = nullptr;
#endif
    std::atomic<long> ref_{1};

    // polling state (main thread only)
    PlayState lastState_ = PlayState::Stopped;
    uint64_t lastId_ = 0;
    double lastPos_ = 0;
    std::chrono::steady_clock::time_point lastPoll_ = std::chrono::steady_clock::now();
    bool forcePush_ = true;
    // the track's tags are read again only when something may have changed (a dozen COM calls each time)
    TrackInfo lastTrack_;
    double lastInfoDur_ = 0;
    bool trackDirty_ = true;
    std::chrono::steady_clock::time_point lastTrackRead_{};
};

// ------------------------------------------------------------------------------------------------ frame impl

HRESULT WINAPI OptionsFrame::GetName(IAIMPString** S) {
    if (!S) return E_POINTER;
    *S = aimp::MakeString(owner_->Core(), L"Discord Rich Presence");
    return *S ? S_OK : E_FAIL;
}

HWND WINAPI OptionsFrame::CreateFrame(HWND parent) {
    if (page_) DestroyFrame();
    owner_->DetectLanguage();
    page_ = SettingsPage::Create(owner_->Core(), parent, [this]() {
        if (service_) service_->FrameModified(this);   // enables AIMP's "Apply" button
    });
    return page_ ? page_->Hwnd() : (HWND)0;
}

void WINAPI OptionsFrame::DestroyFrame() {
    if (page_) {
        page_->Destroy();
        page_ = nullptr;
    }
}

void WINAPI OptionsFrame::Notification(INT32 id) {
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
        case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOCALIZATION:   // AIMP's language was changed
            owner_->DetectLanguage();
            if (page_) page_->Localize();
            break;
        case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_RESET:          // "Reset" button in AIMP's options dialog
            if (page_) {
                Config defaults;
                page_->Load(&defaults);
                if (service_) service_->FrameModified(this);
            }
            break;
        default:
            break;
    }
}

void WINAPI EventHook::CoreMessage(DWORD message, INT32, void*, HRESULT*) {
    if (!owner_) return;
    switch (message) {
        case AIMP_MSG_EVENT_LANGUAGE:
            owner_->DetectLanguage();
            break;
        default:
            break;
    }
    switch (message) {   // the tags have to be read again (Windows: the next timer tick does that)
        case AIMP_MSG_EVENT_STREAM_START:
        case AIMP_MSG_EVENT_STREAM_START_SUBTRACK:
        case AIMP_MSG_EVENT_PLAYING_FILE_INFO:
            owner_->OnTrackEvent();
            break;
        default:
            break;
    }
    switch (message) {
#ifndef _WIN32
        case AIMP_MSG_EVENT_PLAYER_STATE:
        case AIMP_MSG_EVENT_STREAM_START:
        case AIMP_MSG_EVENT_STREAM_START_SUBTRACK:
        case AIMP_MSG_EVENT_STREAM_END:
        case AIMP_MSG_EVENT_PLAYING_FILE_INFO:
        case AIMP_MSG_EVENT_PROPERTY_VALUE:              // e.g. user seeked
        case AIMP_MSG_EVENT_PLAYER_UPDATE_POSITION:      // every second while playing
            owner_->Poll();
            break;
#endif
        default:
            break;
    }
}

void WINAPI PresetAction::OnExecute(IUnknown*) {
    if (!g_plugin) return;
    if (index_ == -2) g_plugin->ToggleByKey();
    else g_plugin->LoadPresetByKey(index_);
}

#ifndef _WIN32
void WINAPI UiTask::Execute(IAIMPTaskOwner*) {
    g_uiPending = false;
    if (g_plugin) g_plugin->OnUiNotify();
}
#endif

}  // namespace

void NotifyUi() {
#ifndef _WIN32
    if (g_uiPending.exchange(true)) return;   // a refresh is on its way already
    std::lock_guard<std::mutex> lk(g_threadsMu);
    if (!g_threads || FAILED(g_threads->ExecuteInMainThread(&g_uiTask, 0))) g_uiPending = false;
#endif
    // Windows: the 500 ms timer refreshes the settings page anyway
}

void PresetsChanged() {
    if (g_plugin) g_plugin->NameActions();
}

namespace {

// ------------------------------------------------------------------------------------------------ plugin impl

HRESULT WINAPI Plugin::Initialize(IAIMPCore* core) {
    core_ = core;
    core_->AddRef();
    g_plugin = this;

    core_->QueryInterface(IID_IAIMPServicePlayer, player_.putVoid());
    core_->QueryInterface(IID_IAIMPServiceMessageDispatcher, dispatcher_.putVoid());
    if (!player_ || !dispatcher_) return E_FAIL;

    web::Reset();
    {   // settings and cache live in AIMP's profile folder (portable AIMP: AIMP\Profile)
        ComPtr<IAIMPString> profile;
        if (SUCCEEDED(core_->GetPath(AIMP_CORE_PATH_PROFILE, profile.put())) && profile)
            config::SetProfileDir(aimp::StringOf(profile.get()));
    }
    config::Load();
    i18n::SetOverride(config::Get().language);
    DetectLanguage();
    if (const int v = aimp::Version(core_)) util::Log(L"AIMP %d.%02d", v / 100, v % 100);
#ifndef _WIN32
    {
        // queued UI tasks may run after Finalize: keep this library loaded until AIMP exits
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&NotifyUi), &info) && info.dli_fname)
            dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
        std::lock_guard<std::mutex> lk(g_threadsMu);
        core_->QueryInterface(IID_IAIMPServiceThreads, reinterpret_cast<void**>(&g_threads));
    }
#endif
    {   // the update check watches the plugin file there, too, after it handed a package to AIMP
        ComPtr<IAIMPString> dir;
        if (SUCCEEDED(core_->GetPath(AIMP_CORE_PATH_PLUGINS, dir.put())) && dir) update::SetPluginsDir(aimp::StringOf(dir.get()));
    }
    Worker().Start();
    jobs::Start();

    // settings tab inside AIMP's options dialog (Windows and Linux)
    frame_ = new OptionsFrame(this);
    if (SUCCEEDED(core_->QueryInterface(IID_IAIMPServiceOptionsDialog, options_.putVoid())) && options_)
        frame_->SetService(options_.get());
    core_->RegisterExtension(IID_IAIMPServiceOptionsDialog, frame_);

    RegisterActions();

    // AIMP messages arrive on AIMP's main thread, so all SDK calls stay there
    hook_ = new EventHook(this);
    if (FAILED(dispatcher_->Hook(hook_))) util::Log(L"Could not hook AIMP events");

#ifdef _WIN32
    // hidden message-only window: its WM_TIMER runs on AIMP's main thread
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
#else
    Poll();
#endif
    return S_OK;
}

HRESULT WINAPI Plugin::Finalize() {
#ifdef _WIN32
    if (timerWnd_) {
        KillTimer(timerWnd_, 1);
        DestroyWindow(timerWnd_);
        timerWnd_ = nullptr;
    }
    UnregisterClassW(L"AIMPDiscordRPCTimerWindow", g_hModule);
#endif
    if (hook_) {
        hook_->Detach();
        if (dispatcher_) dispatcher_->Unhook(hook_);
        hook_->Release();
        hook_ = nullptr;
    }
    for (IAIMPAction* a : actions_) {
        if (core_) core_->UnregisterExtension(a);
        a->Release();
    }
    actions_.clear();
    if (core_ && frame_) {
        core_->UnregisterExtension(frame_);
        frame_->DestroyFrame();
        frame_->SetService(nullptr);
        frame_->Release();
        frame_ = nullptr;
    }
    web::Abort();                  // running downloads end at once, so the threads below stop quickly
    jobs::Stop();
    Worker().Stop();               // clears the presence and joins the thread
    CoverResolver::ShutdownImaging();
#ifndef _WIN32
    {
        std::lock_guard<std::mutex> lk(g_threadsMu);
        if (g_threads) g_threads->Release();
        g_threads = nullptr;
    }
#endif

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

// One action per preset plus "next preset" (AIMP 3.60 has them too). Without the actions API the presets are only in the list.
void Plugin::RegisterActions() {
    for (int i = -2; i < Config::kPresets; ++i) {
        ComPtr<IAIMPAction> action;
        if (FAILED(core_->CreateObject(IID_IAIMPAction, action.putVoid())) || !action) {
            util::Log(L"No hot key actions in this AIMP version");
            return;
        }
        const std::wstring id = i == -2 ? std::wstring(L"aimp_discord_rpc.toggle")
                              : i < 0   ? std::wstring(L"aimp_discord_rpc.preset.next")
                                      : L"aimp_discord_rpc.preset." + std::to_wstring(i + 1);
        aimp::SetPropString(core_, action.get(), AIMP_ACTION_PROPID_ID, id);
        PresetAction* ev = new PresetAction(i);
        action->SetValueAsObject(AIMP_ACTION_PROPID_EVENT, static_cast<IAIMPActionEvent*>(ev));
        ev->Release();
        if (FAILED(core_->RegisterExtension(IID_IAIMPServiceActionManager, action.get()))) {
            util::Log(L"Could not register the hot key action %ls", id.c_str());
            continue;
        }
        actions_.push_back(action.get());
        action->AddRef();
    }
    NameActions();
}

void Plugin::NameActions() {
    if (actions_.empty()) return;
    const Config c = config::Get();
    for (size_t k = 0; k < actions_.size(); ++k) {
        const int i = (int)k - 2;   // first: "on / off", "next preset"
        std::wstring name;
        if (i == -2) name = i18n::T("Act.Toggle");
        else if (i < 0) name = i18n::T("Act.Next");
        else if (c.presets[i].name.empty()) name = util::Subst(i18n::T("Act.Preset"), std::to_wstring(i + 1));
        else name = util::Subst(i18n::T("Act.PresetNamed"), std::to_wstring(i + 1), c.presets[i].name);
        aimp::SetPropString(core_, actions_[k], AIMP_ACTION_PROPID_NAME, name);
        aimp::SetPropString(core_, actions_[k], AIMP_ACTION_PROPID_GROUPNAME, L"Discord Rich Presence");
    }
}

void Plugin::LoadPresetByKey(int index) {
    Config c = config::Get();
    if (index < 0) {   // the next stored preset after the active one
        for (int k = 1; k <= Config::kPresets; ++k) {
            const int i = (c.activePreset - 1 + k + Config::kPresets) % Config::kPresets;
            if (!c.presets[i].name.empty()) {
                index = i;
                break;
            }
        }
        if (index < 0) return;
    }
    if (!LoadPreset(c, index)) {
        util::Log(L"Hot key: preset %d is empty", index + 1);
        return;
    }
    util::Log(L"Hot key: preset %d (%ls)", index + 1, c.presets[index].name.c_str());
    c.manualTrack = lastId_;   // wins over the automatic presets until the track changes
    config::Set(c);
    Worker().Refresh();
    forcePush_ = true;
    if (frame_) frame_->Reload();   // an open settings page shows the loaded texts
}

// Hot key "Discord display on / off": the same as the "Enable" tick on the General tab
void Plugin::ToggleByKey() {
    Config c = config::Get();
    c.enabled = !c.enabled;
    util::Log(L"Hot key: Discord display %ls", c.enabled ? L"on" : L"off");
    config::Set(c);
    Worker().Refresh();
    forcePush_ = true;
    if (frame_) frame_->Reload();
}

// AIMP's language: what the MUI service tells about the loaded language file (matched in i18n.cpp)
void Plugin::DetectLanguage() {
    std::vector<std::wstring> hints;
    ComPtr<IAIMPServiceMUI> mui;
    if (core_ && SUCCEEDED(core_->QueryInterface(IID_IAIMPServiceMUI, mui.putVoid())) && mui) {
        ComPtr<IAIMPString> name;
        if (SUCCEEDED(mui->GetName(name.put())) && name) hints.push_back(aimp::StringOf(name.get()));
        for (const wchar_t* key : {L"FILE\\Name", L"FILE\\Language", L"Info\\Name", L"Info\\Language", L"FILE\\LangID"}) {
            IAIMPString* k = aimp::MakeString(core_, key);
            ComPtr<IAIMPString> v;
            if (k && SUCCEEDED(mui->GetValue(k, v.put())) && v) hints.push_back(aimp::StringOf(v.get()));
            if (k) k->Release();
        }
    }
    std::vector<std::wstring> clean;
    for (const std::wstring& h : hints)
        if (!util::Trim(h).empty()) clean.push_back(util::Trim(h));
    i18n::SetAimpHints(clean);
    NameActions();
}

// main thread: something changed in the background (connection, cover, downloads, update check)
void Plugin::OnUiNotify() {
    if (frame_) frame_->Refresh();
    std::wstring text;
    if (dispatcher_ && update::TakeNotification(text)) {   // shown in AIMP's running line / text display
#ifdef _WIN32
        dispatcher_->Send(AIMP_MSG_CMD_SHOW_NOTIFICATION, 0, const_cast<wchar_t*>(text.c_str()));
#else
        std::string u = util::ToUtf8(text);
        dispatcher_->Send(AIMP_MSG_CMD_SHOW_NOTIFICATION, 0, const_cast<char*>(u.c_str()));
#endif
    }
    std::wstring title, body;
    if (update::TakePopup(title, body)) ShowPopup(title, body);
    if (update::TakeRestart()) RestartAimp();
}

// AIMP's own message window (follows the skin, also on Linux); AIMP 3 has none -> notice in AIMP's display
void Plugin::ShowPopup(const std::wstring& title, const std::wstring& text) {
    static bool showing = false;   // the window is modal: timer / UI tasks keep running meanwhile
    if (showing) return;
    showing = true;
    ComPtr<IAIMPUIMessageDialog> dialog;
    HRESULT hr = E_NOINTERFACE;
    if (core_ && SUCCEEDED(core_->QueryInterface(IID_IAIMPUIMessageDialog, dialog.putVoid())) && dialog) {
        IAIMPString* caption = aimp::MakeString(core_, title);
        IAIMPString* message = aimp::MakeString(core_, text);
        hr = dialog->Execute(HWND(0), caption, message, 0x40 /* MB_ICONINFORMATION */);
        if (caption) caption->Release();
        if (message) message->Release();
    }
    showing = false;
    if (FAILED(hr)) {
        util::Log(L"No message window (0x%08X) - notice in AIMP's display instead", (unsigned)hr);
        update::PopupFailed();
    }
}

// AIMP has installed the update: it only loads the new plugin after a restart (by itself it just offers
// "Restart now" in the preferences), so restart it right away - like that button does
void Plugin::RestartAimp() {
    ComPtr<IAIMPServiceShutdown> shutdown;
    HRESULT hr = E_NOINTERFACE;
    if (core_ && SUCCEEDED(core_->QueryInterface(IID_IAIMPServiceShutdown, shutdown.putVoid())) && shutdown) {
        update::RestartingFor();   // the new AIMP's popup then says why it restarted
        IAIMPString* params = aimp::MakeString(core_, std::wstring());   // command line of the new AIMP: none
        hr = shutdown->Restart(params);
        if (params) params->Release();
    }
    if (SUCCEEDED(hr)) {
        util::Log(L"Restarting AIMP");
        return;
    }
    util::Log(L"AIMP could not be restarted (0x%08X) - restart it to load the update", (unsigned)hr);
    update::RestartFailed();
}

#ifdef _WIN32
LRESULT CALLBACK Plugin::TimerWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) {
        Plugin* self = reinterpret_cast<Plugin*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self) self->Poll();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
#endif

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
    if (player_) return player_->GetState();   // AIMP_PLAYER_STATE_XXX
    alignas(8) uint8_t buf[8] = {0};
    if (FAILED(dispatcher_->Send(AIMP_MSG_PROPERTY_PLAYER_STATE, AIMP_MSG_PROPVALUE_GET, buf))) return kPlayerStopped;
    int32_t v;
    memcpy(&v, buf, 4);
    return v;
}

bool Plugin::ReadTrack(TrackInfo& t, double& duration) {
    ComPtr<IAIMPPlaylistItem> item;
    if (SUCCEEDED(player_->GetPlaylistItem(item.put())) && item) {
        t.fileName = PropString(item.get(), AIMP_PLAYLISTITEM_PROPID_FILENAME);
        ComPtr<IAIMPPlaylist> playlist;   // its name: for the playlist filter and %playlist%
        ComPtr<IAIMPPropertyList> props;
        if (SUCCEEDED(item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_PLAYLIST, IID_IAIMPPlaylist, playlist.putVoid())) &&
            playlist && SUCCEEDED(playlist->QueryInterface(IID_IAIMPPropertyList, props.putVoid())) && props)
            t.playlist = PropString(props.get(), AIMP_PLAYLIST_PROPID_NAME);
    }

    // Prefer the player's live info: it also has the current song of internet radio streams and works
    // when playback was started outside a playlist (e.g. music library). Fall back to the playlist item.
    ComPtr<IAIMPFileInfo> info;
    if (FAILED(player_->GetInfo(info.put())) || !info) {
        info.reset();
        if (item) item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, info.putVoid());
    }
    if (!info && !item) return false;

    if (info) {
        t.artist      = PropString(info.get(), AIMP_FILEINFO_PROPID_ARTIST);
        t.albumArtist = PropString(info.get(), AIMP_FILEINFO_PROPID_ALBUMARTIST);
        t.title       = PropString(info.get(), AIMP_FILEINFO_PROPID_TITLE);
        t.album       = PropString(info.get(), AIMP_FILEINFO_PROPID_ALBUM);
        t.genre       = PropString(info.get(), AIMP_FILEINFO_PROPID_GENRE);
        t.year        = PropString(info.get(), AIMP_FILEINFO_PROPID_DATE);
        t.trackNumber = PropString(info.get(), AIMP_FILEINFO_PROPID_TRACKNUMBER);
        if (t.fileName.empty()) t.fileName = PropString(info.get(), AIMP_FILEINFO_PROPID_FILENAME);
        double d = 0;
        if (SUCCEEDED(info->GetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION, &d)) && d > 0) duration = d;
        if (config::Get().advancedTemplates) ReadExtra(info.get(), item.get(), t);
    }
    if (t.title.empty()) t.title = util::FileNameNoExt(t.fileName);   // untagged files / streams
    if (UsesPlaceholder(config::Get(), L"next")) t.next = ReadNext();
    return true;
}

// AIMP's playback queue knows the next track (queue, shuffle and playlist order included), also in
// AIMP 3.60. Without the service %next% stays empty.
std::wstring Plugin::ReadNext() {
    ComPtr<IAIMPServicePlaybackQueue> queue;
    ComPtr<IAIMPPlaybackQueueItem> next;
    ComPtr<IAIMPPlaylistItem> item;
    if (FAILED(core_->QueryInterface(IID_IAIMPServicePlaybackQueue, queue.putVoid())) || !queue ||
        FAILED(queue->GetNextTrack(next.put())) || !next ||
        FAILED(next->GetValueAsObject(AIMP_PLAYBACKQUEUEITEM_PROPID_PLAYLISTITEM, IID_IAIMPPlaylistItem, item.putVoid())) ||
        !item)
        return std::wstring();
    std::wstring artist, title;
    ComPtr<IAIMPFileInfo> info;
    if (SUCCEEDED(item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, info.putVoid())) && info) {
        artist = util::Trim(PropString(info.get(), AIMP_FILEINFO_PROPID_ARTIST));
        title = util::Trim(PropString(info.get(), AIMP_FILEINFO_PROPID_TITLE));
    }
    if (title.empty()) title = util::FileNameNoExt(PropString(item.get(), AIMP_PLAYLISTITEM_PROPID_FILENAME));
    return artist.empty() ? title : artist + L" - " + title;
}

// AIMP's own formatter for one macro ("%PlayCount"): AIMP knows values that are no tag (statistics)
std::wstring Plugin::FormatMacro(IAIMPFileInfo* info, const wchar_t* macro) {
    ComPtr<IAIMPServiceFileInfoFormatter> fmt;
    if (!info || FAILED(core_->QueryInterface(IID_IAIMPServiceFileInfoFormatter, fmt.putVoid())) || !fmt) return {};
    ComPtr<IAIMPString> tmpl;
    *tmpl.put() = aimp::MakeString(core_, macro);
    if (!tmpl) return {};
    for (int id : {AIMP_FILEINFO_FORMATTER_ID_PLAYING, AIMP_FILEINFO_FORMATTER_ID_BASIC}) {
        ComPtr<IAIMPString> out;
        if (SUCCEEDED(fmt->Format(tmpl.get(), info, id, nullptr, out.put())) && out) {
            const std::wstring r = util::Trim(aimp::StringOf(out.get()));
            if (!r.empty() && r.find(L'%') == std::wstring::npos) return r;
        }
    }
    return {};
}

// More fields for the advanced placeholders (Display tab). Read only when they are switched on.
void Plugin::ReadExtra(IAIMPFileInfo* info, IAIMPPlaylistItem* item, TrackInfo& t) {
    static const struct { const wchar_t* name; int id; } kText[] = {
        {L"comment", AIMP_FILEINFO_PROPID_COMMENT},     {L"composer", AIMP_FILEINFO_PROPID_COMPOSER},
        {L"lyricist", AIMP_FILEINFO_PROPID_LYRICIST},   {L"conductor", AIMP_FILEINFO_PROPID_CONDUCTOR},
        {L"copyright", AIMP_FILEINFO_PROPID_COPYRIGHT}, {L"mood", AIMP_FILEINFO_PROPID_MOOD},
        {L"codec", AIMP_FILEINFO_PROPID_CODEC},         {L"disc", AIMP_FILEINFO_PROPID_DISKNUMBER},
        {L"disctotal", AIMP_FILEINFO_PROPID_DISKTOTAL}, {L"tracktotal", AIMP_FILEINFO_PROPID_TRACKTOTAL},
        {L"isrc", AIMP_FILEINFO_PROPID_ISRC},           {L"key", AIMP_FILEINFO_PROPID_KEY},
    };
    static const struct { const wchar_t* name; int id; } kInt[] = {
        {L"bitrate", AIMP_FILEINFO_PROPID_BITRATE},   {L"samplerate", AIMP_FILEINFO_PROPID_SAMPLERATE},
        {L"channels", AIMP_FILEINFO_PROPID_CHANNELS}, {L"bitdepth", AIMP_FILEINFO_PROPID_BITDEPTH},
        {L"bpm", AIMP_FILEINFO_PROPID_BPM},           {L"rating", AIMP_FILEINFO_PROPID_RATING},
    };
    for (const auto& f : kText) {
        std::wstring v = PropString(info, f.id);
        std::wstring line;   // Discord shows one line: line breaks / tabs become one space
        for (wchar_t c : v) {
            if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
            if (c != L' ' || line.empty() || line.back() != L' ') line += c;
        }
        t.extra[f.name] = util::Trim(line);
    }
    if (t.extra[L"codec"].empty()) {   // AIMP often has no codec name (e.g. MP3): the file type instead
        std::wstring ext = util::FileExt(t.fileName);
        for (wchar_t& c : ext) c = (wchar_t)towupper(c);
        if (!util::IsUrl(t.fileName)) t.extra[L"codec"] = ext;
    }
    for (const auto& f : kInt) {
        INT32 v = 0;
        t.extra[f.name] = SUCCEEDED(info->GetValueAsInt32(f.id, &v)) && v > 0 ? std::to_wstring(v) : std::wstring();
    }
    INT64 size = 0;
    if (SUCCEEDED(info->GetValueAsInt64(AIMP_FILEINFO_PROPID_FILESIZE, &size)) && size > 0) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%.2f MB", (double)size / (1024.0 * 1024.0));
        t.extra[L"filesize"] = buf;
        t.extra[L"filesizebytes"] = std::to_wstring((long long)size);
    } else {
        t.extra[L"filesize"] = t.extra[L"filesizebytes"] = std::wstring();
    }
    // play count: AIMP's statistics (what AIMP itself shows as %PlayCount), else the music library's value
    std::wstring count = FormatMacro(info, L"%PlayCount");
    for (wchar_t c : count) if (c < L'0' || c > L'9') { count.clear(); break; }
    if (count.empty()) {
        INT32 v = 0;
        ComPtr<IAIMPFileInfo> itemInfo;
        if (SUCCEEDED(info->GetValueAsInt32(AIMP_FILEINFO_PROPID_ML_PLAYCOUNT, &v)) && v > 0) count = std::to_wstring(v);
        else if (item && SUCCEEDED(item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, itemInfo.putVoid())) &&
                 itemInfo && SUCCEEDED(itemInfo->GetValueAsInt32(AIMP_FILEINFO_PROPID_ML_PLAYCOUNT, &v)) && v > 0)
            count = std::to_wstring(v);
    }
    t.extra[L"playcount"] = count;
}

void Plugin::Poll() {
    using namespace std::chrono;
    OnUiNotify();   // settings page (if open): status, previews (may show a modal window)
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
        trackDirty_ = true;   // the next track is read at once
    } else {
        double pos = 0, dur = 0;
        if (FAILED(player_->GetPosition(&pos)) || !std::isfinite(pos) || pos < 0)
            pos = ReadReal(AIMP_MSG_PROPERTY_PLAYER_POSITION);
        if (FAILED(player_->GetDuration(&dur)) || !std::isfinite(dur) || dur < 0) dur = 0;
        if (ps != lastState_) push = true;                  // play <-> pause
        double expected = lastPos_ + (lastState_ == PlayState::Playing ? duration<double>(now - lastPoll_).count() : 0.0);
        if (std::fabs(pos - expected) > 1.5) push = true;   // user seeked / track restarted / next track

        // tags: on AIMP's track events, on any change above and every 3 s (title of a radio stream, edited tags)
        if (push || trackDirty_ || lastId_ == 0 || now - lastTrackRead_ >= seconds(3)) {
            TrackInfo t;
            double infoDur = 0;
            if (!ReadTrack(t, infoDur)) return;
            trackDirty_ = false;
            lastTrackRead_ = now;
            lastInfoDur_ = infoDur;
            uint64_t id = util::Fnv1a(t.fileName + L"|" + t.artist + L"|" + t.title + L"|" + t.album + L"|" + t.playlist);
            if (id != lastId_) push = true;                 // new track
            else if (t.extra != lastTrack_.extra || t.next != lastTrack_.next) push = true;   // e.g. the play count went up
            lastId_ = id;
            lastTrack_ = std::move(t);
        }
        if (dur <= 0) dur = lastInfoDur_;
        if (dur <= 0) dur = ReadReal(AIMP_MSG_PROPERTY_PLAYER_DURATION);

        s.position = pos;
        s.duration = dur;
        s.trackId = lastId_;
        if (push) s.track = lastTrack_;
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

#ifdef _WIN32
extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

// Exported without decoration through aimp_discord_rpc.def (32-bit stdcall would otherwise be "_AIMPPluginGetHeader@4")
extern "C" HRESULT WINAPI AIMPPluginGetHeader(IAIMPPlugin** header) {
#else
extern "C" __attribute__((visibility("default"))) HRESULT AIMPPluginGetHeader(IAIMPPlugin** header) {
#endif
    if (!header) return E_POINTER;
    *header = new Plugin();
    return S_OK;
}
