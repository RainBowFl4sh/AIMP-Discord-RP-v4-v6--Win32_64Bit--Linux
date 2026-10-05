#include "settings_ui.h"

#include <algorithm>
#include <atomic>
#include <cwchar>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#include "i18n.h"
#include "jobs.h"
#include "update.h"
#include "util.h"
#include "version.h"

const char* EmbeddedChangelog();   // i18n.cpp (CHANGELOG.md, embedded at build time)

using aimp::SameIID;
using i18n::T;

namespace {

enum Ids {
    // General
    IDC_ENABLE = 200, IDC_TYPE, IDC_STATUSDISP, IDC_TIMESTAMPS, IDC_PAUSED, IDC_PAUSEDMIN, IDC_HIDESTREAMS, IDC_EXCLUDE,
    IDC_STATUS,
    // Display
    IDC_DETAILS = 300, IDC_STATE, IDC_LARGETEXT, IDC_SMALLTEXT, IDC_SMALLICON, IDC_BARLEN, IDC_REFRESH, IDC_TITLELINK,
    IDC_TITLELINKCUSTOM, IDC_TITLELINKURL, IDC_PREVIEW,
    // Cover art
    IDC_COVER = 400, IDC_EMBEDDED, IDC_FOLDER, IDC_FOLDERNAMES, IDC_UPLOADHOST, IDC_IMGURID, IDC_PREFERLOCAL,
    IDC_CLEARCACHE, IDC_CACHEINFO, IDC_COVERIMG, IDC_COVERINFO, IDC_COVERLINK,
    // Online sources
    IDC_SPOTIFY = 600, IDC_SPOTIFYID, IDC_SPOTIFYSECRET, IDC_DEEZER, IDC_ITUNES, IDC_BANDCAMP, IDC_DISCOGS,
    IDC_DISCOGSTOKEN, IDC_MUSICBRAINZ,
    // Advanced
    IDC_CONN = 700, IDC_TRANSPORT, IDC_TEST, IDC_RECONNECT, IDC_ACTIONINFO, IDC_LOG, IDC_EXCLUDEPL, IDC_LANG,
    IDC_CUSTOMAPP, IDC_CLIENTIDLABEL, IDC_CLIENTID, IDC_OPENPORTAL, IDC_EXPORT, IDC_IMPORT, IDC_OPENFOLDER, IDC_IOINFO,
    // About
    IDC_AVATAR = 800, IDC_ABOUTTITLE, IDC_ABOUTVERSION, IDC_UPDCHECK, IDC_UPDFREQ, IDC_UPDAUTO, IDC_CHECKNOW,
    IDC_UPDSTATUS, IDC_INSTALL, IDC_NOTES, IDC_PKGFOLDER, IDC_CHANGELOG,
};

enum Kind { kLabel, kCheck, kEdit, kCombo, kButton, kMemo, kImage, kPaint };

const int kRowH = 22;   // height of edits / combos
const int kTabDisplay = 1;

#ifdef _WIN32
const wchar_t* const kNewLine = L"\r\n";
#else
const wchar_t* const kNewLine = L"\n";
#endif

std::wstring RepoUrl() { return L"https://github.com/" + util::FromUtf8(AIMP_DISCORD_RPC_REPO); }

// Forwards the events of one control: changes (check box, edit, combo box, button click) and drawing (paint box).
class Events final : public IAIMPUIChangeEvents, public IAIMPUIDrawEvents {
public:
    Events(SettingsPage* page, int id) : page_(page), id_(id) {}

    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown) || SameIID(riid, IID_IAIMPUIChangeEvents)) {
            *ppv = static_cast<IAIMPUIChangeEvents*>(this);
        } else if (SameIID(riid, IID_IAIMPUIDrawEvents)) {
            *ppv = static_cast<IAIMPUIDrawEvents*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    DWORD __unknwncall AddRef() override { return (DWORD)++ref_; }
    DWORD __unknwncall Release() override {
        DWORD r = (DWORD)--ref_;
        if (r == 0) delete this;
        return r;
    }
    void WINAPI OnChanged(IUnknown*) override {
        if (page_) page_->OnControlChanged(id_);
    }
#ifdef _WIN32
    void WINAPI OnDraw(IUnknown*, HCANVAS canvas, const RECT& r) override {   // by reference: see apiGUI.h
#else
    void WINAPI OnDraw(IUnknown*, HCANVAS canvas, const RECT r) override {
#endif
        if (page_) page_->OnDraw(id_, canvas, r);
    }
    void Detach() { page_ = nullptr; }

private:
    SettingsPage* page_;
    int id_;
    std::atomic<long> ref_{1};
};

// The form needs an events object; nothing to do in it.
class FormEvents final : public IAIMPUIFormEvents {
public:
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown) || SameIID(riid, IID_IAIMPUIFormEvents)) {
            *ppv = static_cast<IAIMPUIFormEvents*>(this);
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
    void WINAPI OnActivated(IAIMPUIForm*) override {}
    void WINAPI OnDeactivated(IAIMPUIForm*) override {}
    void WINAPI OnCreated(IAIMPUIForm*) override {}
    void WINAPI OnDestroyed(IAIMPUIForm*) override {}
    void WINAPI OnCloseQuery(IAIMPUIForm*, BOOL* canClose) override { if (canClose) *canClose = 1; }
    void WINAPI OnLocalize(IAIMPUIForm*) override {}
    void WINAPI OnShortCut(IAIMPUIForm*, WORD, WORD, BOOL*) override {}

private:
    std::atomic<long> ref_{1};
};

const int kPages = 6;
const char* const kTabKeys[kPages] = {"Tab.General", "Tab.Display", "Tab.Cover", "Tab.Sources", "Tab.Advanced",
                                      "Tab.About"};

bool OnlyChars(const std::string& s, const char* allowed) {
    return !s.empty() && s.find_first_not_of(allowed) == std::string::npos;
}

std::wstring Platform() {
#if defined(_WIN64)
    std::wstring p = L"Windows x64";
#elif defined(_WIN32)
    std::wstring p = L"Windows x86";
#else
    std::wstring p = L"Linux x86_64";
#endif
    return util::UnderWine() ? p + L" (Wine)" : p;
}

std::wstring StatusText(const PresenceStatus& st) {
    switch (st.kind) {
        case PresenceStatus::Disabled: return T("St.Disabled");
        case PresenceStatus::Connected: {
            std::wstring name = util::FromUtf8(st.user.globalName.empty() ? st.user.name : st.user.globalName);
            std::wstring s = name.empty() ? T("St.Connected") : util::Subst(T("St.ConnectedAs"), name);
            return st.testing ? s + L" - " + T("St.Testing") : s;
        }
        case PresenceStatus::NotConnected: {
            std::wstring detail = st.errorDetail.empty() ? std::wstring() : L" (" + util::FromUtf8(st.errorDetail) + L")";
            switch (st.error) {
                case IpcError::Rejected:  return util::Subst(T("St.Rejected"), detail);
                case IpcError::Closed:    return util::Subst(T("St.Closed"), detail);
                case IpcError::Handshake:
                case IpcError::NoAnswer:  return T("St.NoAnswer");
                default:                  return T("St.NotRunning");
            }
        }
        default: return T("St.Waiting");
    }
}

// "embedded|catbox.moe" -> "cover in the file's tags, uploaded to catbox.moe"; "Deezer" -> "found on Deezer"
std::wstring SourceText(const std::string& source) {
    if (source.empty()) return T("Cov.SrcCache");
    size_t bar = source.find('|');
    if (bar == std::string::npos) return util::Subst(T("Cov.SrcOnline"), util::FromUtf8(source));
    std::wstring local = T(source.compare(0, bar, "folder") == 0 ? "Cov.SrcFolder" : "Cov.SrcEmbedded");
    return util::Subst(T("Cov.SrcUploaded"), local, util::FromUtf8(source.substr(bar + 1)));
}

std::wstring HiddenText(Hidden h) {
    switch (h) {
        case Hidden::Disabled:       return T("Prev.HiddenDisabled");
        case Hidden::Stream:         return T("Prev.HiddenStream");
        case Hidden::PathFilter:     return T("Prev.HiddenPath");
        case Hidden::PlaylistFilter: return T("Prev.HiddenPlaylist");
        case Hidden::Paused:         return T("Prev.HiddenPaused");
        case Hidden::PausedTimeout:  return T("Prev.HiddenTimeout");
        default:                     return T("Prev.HiddenStopped");
    }
}

// "  - text ..." -> lines of at most 'width' characters, continuation lines indented like the text
std::wstring Wrap(const std::wstring& text, size_t width) {
    size_t indent = text.find_first_not_of(L" \u2022-");
    if (indent == std::wstring::npos) indent = 0;
    std::wstring out, line;
    auto emit = [&] {
        out += (out.empty() ? text.substr(0, indent) : std::wstring(indent, L' ')) + line + kNewLine;
        line.clear();
    };
    for (const std::wstring& word : util::Split(text.substr(indent), L' ')) {
        if (!line.empty() && indent + line.size() + 1 + word.size() > width) emit();
        line += (line.empty() ? L"" : L" ") + word;
    }
    emit();
    return out;
}

// CHANGELOG.md -> plain text for the memo on the About tab (the newest version - the installed one - first)
std::wstring ChangelogText() {
    std::wstring out, item;
    const std::wstring lang = i18n::Active();
    if (lang == L"ru" || lang == L"uk") out = Wrap(T("About.AiNote"), 76) + kNewLine;   // AI translation
    bool first = true;
    auto flush = [&] {
        if (!item.empty()) out += Wrap(item, 76);
        item.clear();
    };
    for (std::wstring line : util::Split(util::FromUtf8(EmbeddedChangelog()), L'\n')) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const bool nested = line.rfind(L"  ", 0) == 0;
        std::wstring t = util::ReplaceAll(util::ReplaceAll(util::Trim(line), L"*", L""), L"`", L"");   // Markdown
        if (t.rfind(L"## ", 0) == 0) {
            flush();
            std::wstring v = t.substr(3);
            if (!first) out += kNewLine;
            out += util::Subst(T("About.ChangelogVersion"), v);
            if (util::FindVersion(v) == AIMP_DISCORD_RPC_VERSION_W) out += L"  " + T("About.Installed");
            out += kNewLine;
            first = false;
        } else if (t.rfind(L"### ", 0) == 0) {
            flush();
            out += t.substr(4) + L":" + kNewLine;
        } else if (t.rfind(L"- ", 0) == 0) {
            flush();
            item = (nested ? L"      - " : L"  • ") + t.substr(2);
        } else if (!t.empty() && !item.empty()) {
            item += L" " + t;   // continuation line of a list item
        } else {
            flush();
        }
    }
    flush();
    return out;
}

}  // namespace

// ================================================================================ creation

SettingsPage* SettingsPage::Create(IAIMPCore* core, HWND parent, std::function<void()> onModified) {
    if (!core) return nullptr;
    SettingsPage* p = new SettingsPage();
    p->core_ = core;
    p->onModified_ = std::move(onModified);
    if (!p->Build(parent)) {
        p->Destroy();
        return nullptr;
    }
    p->Load();
    // picture for the About tab (from the disk cache, else downloaded in the background)
    jobs::FetchImage("author", L"https://avatars.githubusercontent.com/u/" + util::FromUtf8(AIMP_DISCORD_RPC_GITHUB_ID) +
                                   L"?s=128&v=4",
                     L"author.png", 7);
    return p;
}

bool SettingsPage::Build(HWND parent) {
    if (FAILED(core_->QueryInterface(IID_IAIMPServiceUI, reinterpret_cast<void**>(&ui_))) || !ui_) {
        util::Log(L"AIMP UI service not available - no settings page");
        return false;
    }
    formEvents_ = new FormEvents();
    IAIMPString* name = aimp::MakeString(core_, L"DiscordRPCSettings");
    HRESULT hr = ui_->CreateForm(parent, AIMPUI_SERVICE_CREATEFORM_FLAGS_CHILD, name, formEvents_, &form_);
    if (name) name->Release();
    if (FAILED(hr) || !form_) {
        util::Log(L"Could not create the settings form (0x%08x)", (unsigned)hr);
        form_ = nullptr;
        return false;
    }
    form_->SetValueAsInt32(AIMPUI_FORM_PROPID_BORDERSTYLE, AIMPUI_FLAGS_BORDERSTYLE_NONE);
#ifdef _WIN32
    if (parent) {   // fill AIMP's frame area; AIMP resizes it afterwards together with the dialog
        RECT rc;
        GetClientRect(parent, &rc);
        TAIMPUIControlPlacement pl = {};
        pl.Alignment = ualNone;
        pl.Bounds = rc;
        form_->SetPlacement(pl);
    }
#endif

    // tabs fill the whole form
    name = aimp::MakeString(core_, L"Tabs");
    hr = ui_->CreateControl(form_, form_, name, nullptr, IID_IAIMPUIPageControl, reinterpret_cast<void**>(&tabs_));
    if (name) name->Release();
    if (FAILED(hr) || !tabs_) {
        tabs_ = nullptr;
        return false;
    }
    TAIMPUIControlPlacement pl = {};
    pl.Alignment = ualClient;
    tabs_->SetPlacement(pl);
    anonymous_.push_back(tabs_);

    IAIMPUITabSheet* sheets[kPages] = {};
    for (int i = 0; i < kPages; ++i) {
        name = aimp::MakeString(core_, L"Page" + std::to_wstring(i));
        hr = tabs_->Add(name, &sheets[i]);
        if (name) name->Release();
        if (FAILED(hr) || !sheets[i]) return false;
        anonymous_.push_back(sheets[i]);
        Text(sheets[i], AIMPUI_TABSHEET_PROPID_CAPTION, kTabKeys[i]);
    }
    BuildGeneral(sheets[0]);
    BuildDisplay(sheets[1]);
    BuildCover(sheets[2]);
    BuildSources(sheets[3]);
    BuildAdvanced(sheets[4]);
    BuildAbout(sheets[5]);
    tabs_->SetValueAsInt32(AIMPUI_PAGECONTROL_PROPID_ACTIVE, 0);

    hwnd_ = form_->GetHandle();
    return true;
}

void SettingsPage::Destroy() {
    for (IUnknown* h : handlers_) {
        static_cast<Events*>(static_cast<IAIMPUIChangeEvents*>(h))->Detach();
        h->Release();
    }
    handlers_.clear();
    for (auto& kv : images_)
        if (kv.second) kv.second->Release();
    images_.clear();
    for (auto& kv : items_) kv.second->Release();
    items_.clear();
    for (IAIMPUIControl* c : anonymous_) c->Release();
    anonymous_.clear();
    texts_.clear();
    tabs_ = nullptr;
    if (form_) {
        form_->Destroy(0);
        // AIMP 4 frees the form object together with the form ("Invalid pointer operation" on a further Release)
        const int v = aimp::Version(core_);
        if (v == 0 || v >= 500) form_->Release();
        form_ = nullptr;
    }
    if (formEvents_) {
        formEvents_->Release();
        formEvents_ = nullptr;
    }
    if (ui_) {
        ui_->Release();
        ui_ = nullptr;
    }
    hwnd_ = 0;
    jobs::ForgetImages();
    delete this;
}

// ================================================================================ control factory

IAIMPUIControl* SettingsPage::Create(IAIMPUIWinControl* parent, int id, REFIID iid, bool events) {
    IUnknown* handler = nullptr;
    if (events) {
        handler = static_cast<IAIMPUIChangeEvents*>(new Events(this, id));
        handlers_.push_back(handler);
    }
    IAIMPString* name = aimp::MakeString(core_, L"c" + std::to_wstring(++nextName_));
    void* obj = nullptr;
    HRESULT hr = ui_->CreateControl(form_, parent, name, handler, iid, &obj);
    if (name) name->Release();
    if (FAILED(hr) || !obj) return nullptr;
    // every control interface derives from IAIMPUIControl as its first (only) base
    IAIMPUIControl* c = static_cast<IAIMPUIControl*>(obj);
    if (id) {
        auto old = items_.find(id);
        if (old != items_.end()) old->second->Release();
        items_[id] = c;
    } else {
        anonymous_.push_back(c);
    }
    return c;
}

void SettingsPage::Place(IAIMPUIControl* c, int x, int y, int w, int h) {
    if (!c) return;
    TAIMPUIControlPlacement pl = {};
    pl.Alignment = ualNone;
    // Anchors (left / top / right / bottom = 0 or 1): anchor to the top-left corner. Without anchors AIMP's UI
    // (like Delphi's VCL) keeps a control's relative position when the tab sheet grows from its initial small
    // size to the dialog size - every control then drifts to the middle or out of view.
    pl.Anchors.left = 1;
    pl.Anchors.top = 1;
    pl.Bounds.left = x;
    pl.Bounds.top = y;
    pl.Bounds.right = x + w;
    pl.Bounds.bottom = y + h;
    c->SetPlacement(pl);
}

void SettingsPage::Text(IAIMPUIControl* c, int prop, const char* key) {
    if (!c || !key) return;
    aimp::SetPropString(core_, c, prop, T(key));
    texts_.push_back({c, prop, key});
}

void SettingsPage::Label(IAIMPUIWinControl* p, const char* key, int x, int y, int w, int h, int id) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUILabel, false);
    if (!c) return;
    if (id) kinds_[id] = kLabel;
    c->SetValueAsInt32(AIMPUI_LABEL_PROPID_AUTOSIZE, 0);
    c->SetValueAsInt32(AIMPUI_LABEL_PROPID_WORDWRAP, h > 20 ? 1 : 0);
    Text(c, AIMPUI_LABEL_PROPID_TEXT, key);
    Place(c, x, y, w, h);
}

void SettingsPage::Link(IAIMPUIWinControl* p, const char* key, const std::wstring& url, int x, int y, int w, int id) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUILabel, false);
    if (!c) return;
    if (id) kinds_[id] = kLabel;
    c->SetValueAsInt32(AIMPUI_LABEL_PROPID_AUTOSIZE, 0);
    Text(c, AIMPUI_LABEL_PROPID_TEXT, key);
    if (!url.empty()) aimp::SetPropString(core_, c, AIMPUI_LABEL_PROPID_URL, url);   // AIMP opens it on click
    Place(c, x, y, w, 18);
}

void SettingsPage::Check(IAIMPUIWinControl* p, int id, const char* key, int x, int y, int w) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUICheckBox, true);
    if (!c) return;
    kinds_[id] = kCheck;
    c->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_AUTOSIZE, 0);
    Text(c, AIMPUI_CHECKBOX_PROPID_CAPTION, key);
    Place(c, x, y, w, 20);
}

void SettingsPage::Edit(IAIMPUIWinControl* p, int id, int x, int y, int w, bool password) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIEdit, true);
    if (!c) return;
    kinds_[id] = kEdit;
    if (password) c->SetValueAsInt32(AIMPUI_EDIT_PROPID_PASSWORDCHAR, '*');
    Place(c, x, y, w, kRowH);
}

void SettingsPage::Combo(IAIMPUIWinControl* p, int id, std::initializer_list<const char*> keys, int x, int y, int w) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIComboBox, true);
    if (!c) return;
    kinds_[id] = kCombo;
    c->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_STYLE, AIMPUI_COMBOBOX_STYLE_LIST);
    comboKeys_[id] = std::vector<const char*>(keys);
    for (const char* key : keys) {
        IAIMPString* s = aimp::MakeString(core_, T(key));
        if (!s) continue;
        static_cast<IAIMPUIComboBox*>(c)->Add(s, 0);
        s->Release();
    }
    Place(c, x, y, w, kRowH);
}

void SettingsPage::Button(IAIMPUIWinControl* p, int id, const char* key, int x, int y, int w) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIButton, true);
    if (!c) return;
    kinds_[id] = kButton;
    Text(c, AIMPUI_BUTTON_PROPID_CAPTION, key);
    Place(c, x, y, w, 25);
}

void SettingsPage::Memo(IAIMPUIWinControl* p, int id, int x, int y, int w, int h) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIMemo, false);
    if (!c) return;
    kinds_[id] = kMemo;
    c->SetValueAsInt32(AIMPUI_BASEEDIT_PROPID_READONLY, 1);
    Place(c, x, y, w, h);
}

void SettingsPage::Paint(IAIMPUIWinControl* p, int id, int x, int y, int w, int h) {
    if (IAIMPUIControl* c = Create(p, id, IID_IAIMPUIPaintBox, true)) {
        kinds_[id] = kPaint;
        Place(c, x, y, w, h);
    }
}

void SettingsPage::Image(IAIMPUIWinControl* p, int id, int x, int y, int w, int h) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIImage, false);
    if (!c) return;
    kinds_[id] = kImage;
    c->SetValueAsInt32(AIMPUI_IMAGE_PROPID_IMAGESTRETCHMODE, AIMP_IMAGE_DRAW_STRETCHMODE_FIT | AIMP_IMAGE_DRAW_QUALITY_HIGH);
    c->SetValueAsInt32(AIMPUI_CONTROL_PROPID_VISIBLE, 0);   // shown as soon as the picture is there
    Place(c, x, y, w, h);
}

// ================================================================================ value access

IAIMPUIControl* SettingsPage::Item(int id) const {
    auto it = items_.find(id);
    return it == items_.end() ? nullptr : it->second;
}

void SettingsPage::SetCheck(int id, bool v) {
    if (IAIMPUIControl* c = Item(id))
        c->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, v ? AIMPUI_CHECKSTATE_CHECKED : AIMPUI_CHECKSTATE_UNCHECKED);
}
bool SettingsPage::GetCheck(int id) const {
    INT32 v = 0;
    IAIMPUIControl* c = Item(id);
    return c && SUCCEEDED(c->GetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, &v)) && v == AIMPUI_CHECKSTATE_CHECKED;
}

void SettingsPage::SetText(int id, const std::wstring& s) {
    IAIMPUIControl* c = Item(id);
    if (!c) return;
    auto k = kinds_.find(id);
    const int kind = k == kinds_.end() ? kEdit : k->second;
    const int prop = kind == kLabel  ? AIMPUI_LABEL_PROPID_TEXT
                   : kind == kButton ? AIMPUI_BUTTON_PROPID_CAPTION
                   : kind == kCheck  ? AIMPUI_CHECKBOX_PROPID_CAPTION
                                     : AIMPUI_BASEEDIT_PROPID_TEXT;
    aimp::SetPropString(core_, c, prop, s);
}
std::wstring SettingsPage::GetText(int id) const { return aimp::PropString(Item(id), AIMPUI_BASEEDIT_PROPID_TEXT); }

void SettingsPage::SetSel(int id, int index) {
    if (IAIMPUIControl* c = Item(id)) c->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_ITEMINDEX, index);
}
int SettingsPage::GetSel(int id) const {
    INT32 v = -1;
    IAIMPUIControl* c = Item(id);
    if (!c || FAILED(c->GetValueAsInt32(AIMPUI_COMBOBOX_PROPID_ITEMINDEX, &v))) return -1;
    return v;
}

int SettingsPage::GetInt(int id, int def) const {
    std::wstring t = util::Trim(GetText(id));
    if (t.empty()) return def;
    wchar_t* end = nullptr;
    long v = wcstol(t.c_str(), &end, 10);
    return (end == t.c_str()) ? def : (int)v;
}

void SettingsPage::SetVisible(int id, bool v) {
    if (IAIMPUIControl* c = Item(id)) c->SetValueAsInt32(AIMPUI_CONTROL_PROPID_VISIBLE, v ? 1 : 0);
}
void SettingsPage::SetEnabled(int id, bool v) {
    if (IAIMPUIControl* c = Item(id)) c->SetValueAsInt32(AIMPUI_CONTROL_PROPID_ENABLED, v ? 1 : 0);
}
void SettingsPage::SetUrl(int id, const std::wstring& url) {
    if (IAIMPUIControl* c = Item(id)) aimp::SetPropString(core_, c, AIMPUI_LABEL_PROPID_URL, url);
}
void SettingsPage::SetImage(int id, IAIMPImage* img) {
    IAIMPUIControl* c = Item(id);
    if (!c) return;
    if (img) c->SetValueAsObject(AIMPUI_IMAGE_PROPID_IMAGE, img);
    c->SetValueAsInt32(AIMPUI_CONTROL_PROPID_VISIBLE, img ? 1 : 0);
}

IAIMPImage* SettingsPage::MakeImage(const std::string& bytes) {
    if (bytes.empty()) return nullptr;
    IAIMPMemoryStream* ms = nullptr;
    IAIMPImage* img = nullptr;
    if (FAILED(core_->CreateObject(IID_IAIMPMemoryStream, reinterpret_cast<void**>(&ms))) || !ms) return nullptr;
    DWORD written = 0;
    ms->Write(const_cast<char*>(bytes.data()), (DWORD)bytes.size(), &written);
    ms->Seek(0, AIMP_STREAM_SEEKMODE_FROM_BEGINNING);
    if (FAILED(core_->CreateObject(IID_IAIMPImage, reinterpret_cast<void**>(&img))) || !img ||
        FAILED(img->LoadFromStream(ms))) {
        if (img) img->Release();
        img = nullptr;
    }
    ms->Release();
    return img;
}

int SettingsPage::ActiveTab() const {
    INT32 v = -1;
    if (!tabs_ || FAILED(tabs_->GetValueAsInt32(AIMPUI_PAGECONTROL_PROPID_ACTIVE, &v))) return -1;
    return v;
}

// ================================================================================ layout (fits 465 x 420 at 96 DPI: AIMP's page is about 478 px wide,
// the rest is a right margin like the one on the left)

void SettingsPage::BuildGeneral(IAIMPUIWinControl* p) {
    int y = 8;
    Check(p, IDC_ENABLE, "Gen.Enable", 10, y, 455);                                         y += 28;
    Label(p, "Gen.ActivityType", 10, y + 3, 195);
    Combo(p, IDC_TYPE, {"Gen.TypeListening", "Gen.TypePlaying"}, 210, y, 255);              y += 30;
    Label(p, "Gen.StatusText", 10, y + 3, 195);
    Combo(p, IDC_STATUSDISP, {"Gen.StatusApp", "Gen.StatusState", "Gen.StatusDetails"}, 210, y, 255); y += 32;
    Check(p, IDC_TIMESTAMPS, "Gen.Timestamps", 10, y, 455);                                 y += 26;
    Label(p, "Gen.Paused", 10, y + 3, 195);
    Combo(p, IDC_PAUSED, {"Gen.PausedShow", "Gen.PausedHide"}, 210, y, 255);                y += 30;
    Label(p, "Gen.ClearAfter", 10, y + 3, 390);
    Edit(p, IDC_PAUSEDMIN, 405, y, 60);                                                     y += 30;
    Check(p, IDC_HIDESTREAMS, "Gen.HideStreams", 10, y, 455);                               y += 26;
    Label(p, "Gen.ExcludePaths", 10, y, 455);                                               y += 20;
    Edit(p, IDC_EXCLUDE, 10, y, 455);                                                       y += 34;
    Label(p, nullptr, 10, y, 455, 34, IDC_STATUS);
}

void SettingsPage::BuildDisplay(IAIMPUIWinControl* p) {
    int y = 8;
    Label(p, "Disp.Details", 10, y + 3, 145);   Edit(p, IDC_DETAILS, 160, y, 305);         y += 26;
    Label(p, "Disp.State", 10, y + 3, 145);     Edit(p, IDC_STATE, 160, y, 305);           y += 26;
    Label(p, "Disp.LargeText", 10, y + 3, 145); Edit(p, IDC_LARGETEXT, 160, y, 305);       y += 26;
    Label(p, "Disp.SmallText", 10, y + 3, 145); Edit(p, IDC_SMALLTEXT, 160, y, 305);       y += 28;
    Check(p, IDC_SMALLICON, "Disp.SmallIcon", 10, y, 455);                                  y += 24;
    Label(p, "Disp.BarLength", 10, y + 3, 150); Edit(p, IDC_BARLEN, 165, y, 45);
    Label(p, "Disp.Refresh", 230, y + 3, 170);  Edit(p, IDC_REFRESH, 405, y, 60);           y += 28;
    Label(p, "Disp.Placeholders", 10, y, 455, 46);                                          y += 48;
    Check(p, IDC_TITLELINK, "Disp.TitleLink", 10, y, 455);                                  y += 24;
    Check(p, IDC_TITLELINKCUSTOM, "Disp.OwnLink", 28, y + 1, 127);
    Edit(p, IDC_TITLELINKURL, 160, y, 305);                                                 y += 28;
    // live preview: what Discord will show (drawn in OnDraw)
    Paint(p, IDC_PREVIEW, 10, y, kPreviewWidth, kPreviewHeight);
}

void SettingsPage::BuildCover(IAIMPUIWinControl* p) {
    int y = 8;
    Check(p, IDC_COVER, "Cov.Enable", 10, y, 455);                                          y += 24;
    Label(p, "Cov.Info", 10, y, 455, 62);                                                   y += 66;
    // left: options, right: the current cover and where it comes from
    const int top = y;
    Check(p, IDC_EMBEDDED, "Cov.Embedded", 10, y, 315);                                     y += 24;
    Check(p, IDC_FOLDER, "Cov.Folder", 10, y, 315);                                         y += 24;
    Label(p, "Cov.FolderNames", 28, y + 3, 112); Edit(p, IDC_FOLDERNAMES, 145, y, 180);      y += 30;
    Label(p, "Cov.Upload", 10, y + 3, 130);
    Combo(p, IDC_UPLOADHOST, {"Cov.UploadOff", "Cov.UploadCatbox", "Cov.UploadImgur", "Cov.UploadX0"}, 145, y, 180); y += 30;
    Label(p, "Cov.ImgurId", 28, y + 3, 112);     Edit(p, IDC_IMGURID, 145, y, 100);
    Link(p, "Cov.GetImgurId", L"https://api.imgur.com/oauth2/addclient", 252, y + 3, 73);   y += 30;
    Check(p, IDC_PREFERLOCAL, "Cov.PreferLocal", 10, y, 315);                                y += 24;
    Label(p, "Cov.PublicNote", 10, y, 315, 34);

    Label(p, "Cov.Current", 335, top, 130, 32);
    Image(p, IDC_COVERIMG, 340, top + 34, 120, 120);
    Label(p, nullptr, 335, top + 160, 130, 64, IDC_COVERINFO);
    Link(p, "Cov.OpenImage", std::wstring(), 335, top + 228, 130, IDC_COVERLINK);
    SetVisible(IDC_COVERLINK, false);   // shown with the cover

    y = top + 256;
    Button(p, IDC_CLEARCACHE, "Cov.ClearCache", 10, y, 190);
    Label(p, nullptr, 210, y + 4, 255, 18, IDC_CACHEINFO);
}

void SettingsPage::BuildSources(IAIMPUIWinControl* p) {
    int y = 8;
    Label(p, "Src.Info", 10, y, 455, 50);                                                   y += 54;
    Check(p, IDC_SPOTIFY, "Src.Spotify", 10, y, 330);
    Link(p, "Src.CreateApp", L"https://developer.spotify.com/dashboard", 345, y + 1, 120);   y += 28;
    Label(p, "Src.ClientId", 28, y + 3, 122);     Edit(p, IDC_SPOTIFYID, 155, y, 310);      y += 28;
    Label(p, "Src.ClientSecret", 28, y + 3, 122); Edit(p, IDC_SPOTIFYSECRET, 155, y, 310, true); y += 34;
    Check(p, IDC_DEEZER, "Src.Deezer", 10, y, 455);                                         y += 24;
    Check(p, IDC_ITUNES, "Src.Itunes", 10, y, 455);                                         y += 24;
    Check(p, IDC_BANDCAMP, "Src.Bandcamp", 10, y, 455);                                     y += 28;
    Check(p, IDC_DISCOGS, "Src.Discogs", 10, y, 330);
    Link(p, "Src.GetToken", L"https://www.discogs.com/settings/developers", 345, y + 1, 120); y += 28;
    Label(p, "Src.Token", 28, y + 3, 122);        Edit(p, IDC_DISCOGSTOKEN, 155, y, 310, true); y += 34;
    Check(p, IDC_MUSICBRAINZ, "Src.MusicBrainz", 10, y, 455);
}

void SettingsPage::BuildAdvanced(IAIMPUIWinControl* p) {
    int y = 8;
    Label(p, nullptr, 10, y, 455, 34, IDC_CONN);                                            y += 36;
    Label(p, nullptr, 10, y, 455, 34, IDC_TRANSPORT);                                       y += 38;
    Button(p, IDC_TEST, "Adv.Test", 10, y, 190);
    Button(p, IDC_RECONNECT, "Adv.Reconnect", 210, y, 150);
    Label(p, nullptr, 370, y - 3, 95, 32, IDC_ACTIONINFO);                                 y += 32;
    Label(p, "Adv.Log", 10, y, 455);                                                        y += 18;
    Memo(p, IDC_LOG, 10, y, 455, 92);                                                       y += 100;
    Label(p, "Adv.ExcludePlaylists", 10, y + 3, 290); Edit(p, IDC_EXCLUDEPL, 305, y, 160);   y += 30;
    Label(p, "Adv.Language", 10, y + 3, 128);
    if (IAIMPUIControl* c = Create(p, IDC_LANG, IID_IAIMPUIComboBox, true)) {   // items: FillLanguages
        kinds_[IDC_LANG] = kCombo;
        c->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_STYLE, AIMPUI_COMBOBOX_STYLE_LIST);
        Place(c, 145, y, 240, kRowH);
    }
    y += 32;
    Check(p, IDC_CUSTOMAPP, "Adv.CustomApp", 10, y, 455);                                   y += 26;
    Label(p, "Adv.AppId", 28, y + 3, 122, 18, IDC_CLIENTIDLABEL);
    Edit(p, IDC_CLIENTID, 155, y, 200);
    Link(p, "Adv.Portal", L"https://discord.com/developers/applications", 365, y + 3, 100, IDC_OPENPORTAL); y += 32;
    Label(p, "Adv.SettingsFile", 10, y + 4, 130);
    Button(p, IDC_EXPORT, "Adv.Export", 145, y, 100);
    Button(p, IDC_IMPORT, "Adv.Import", 252, y, 100);
    Link(p, "Adv.OpenFolder", util::FileUrl(config::DataDir()), 362, y + 4, 103, IDC_OPENFOLDER); y += 30;
    Label(p, nullptr, 10, y, 455, 18, IDC_IOINFO);
}

void SettingsPage::BuildAbout(IAIMPUIWinControl* p) {
    Paint(p, IDC_AVATAR, 10, 8, 64, 64);   // drawn with rounded corners (OnDraw)
    Label(p, "About.Title", 86, 8, 379, 26, IDC_ABOUTTITLE);
    if (IAIMPUIControl* c = Item(IDC_ABOUTTITLE)) {
        c->SetValueAsInt32(AIMPUI_LABEL_PROPID_WORDWRAP, 0);
        c->SetValueAsInt32(AIMPUI_LABEL_PROPID_TEXTSTYLE, AIMPUI_FLAGS_FONT_BOLD);
        c->SetValueAsInt32(AIMPUI_LABEL_PROPID_TEXTSIZE, 13);
    }
    Label(p, nullptr, 86, 36, 379, 18, IDC_ABOUTVERSION);
    Link(p, "About.Author", L"https://github.com/RainBowFl4sh", 86, 56, 379);
    int y = 84;
    Link(p, "About.GitHub", RepoUrl(), 10, y, 140);
    Link(p, "About.Releases", RepoUrl() + L"/releases", 160, y, 140);
    Link(p, "About.Issues", RepoUrl() + L"/issues", 310, y, 155);                           y += 28;
    Check(p, IDC_UPDCHECK, "About.UpdateCheck", 10, y, 185);
    Combo(p, IDC_UPDFREQ, {"Upd.EveryStart", "Upd.Daily", "Upd.Weekly", "Upd.Monthly"}, 200, y, 160);
    Button(p, IDC_CHECKNOW, "About.CheckNow", 368, y - 1, 97);                             y += 30;
    Check(p, IDC_UPDAUTO, "About.UpdateAuto", 10, y, 455);                                  y += 26;
    Label(p, nullptr, 10, y, 335, 34, IDC_UPDSTATUS);
    Button(p, IDC_INSTALL, nullptr, 355, y, 110);
    Link(p, "About.WhatsNew", RepoUrl() + L"/releases/latest", 355, y + 28, 110, IDC_NOTES);
    Link(p, "About.ShowFile", std::wstring(), 355, y + 28, 110, IDC_PKGFOLDER);             y += 52;
    Label(p, "About.Changelog", 10, y, 455);                                                y += 18;
    Memo(p, IDC_CHANGELOG, 10, y, 455, 412 - y);
    SetText(IDC_ABOUTVERSION, util::Subst(T("About.Version"), AIMP_DISCORD_RPC_VERSION_W, Platform()));
    SetText(IDC_CHANGELOG, ChangelogText());
    SetVisible(IDC_INSTALL, false);
    SetVisible(IDC_NOTES, false);
    SetVisible(IDC_PKGFOLDER, false);
}

// ================================================================================ behaviour

void SettingsPage::FillLanguages(const std::wstring& selected) {
    IAIMPUIControl* c = Item(IDC_LANG);
    if (!c) return;
    IAIMPUIComboBox* cb = static_cast<IAIMPUIComboBox*>(c);
    cb->Clear();
    langCodes_.assign(1, std::wstring());
    const std::wstring aimp = i18n::AimpLanguage();
    std::vector<std::wstring> items = {aimp.empty() ? T("Adv.LangAutoUnknown") : util::Subst(T("Adv.LangAuto"), aimp)};
    for (const i18n::Language& l : i18n::Available()) {
        langCodes_.push_back(l.code);
        items.push_back(l.name);
    }
    int sel = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (IAIMPString* s = aimp::MakeString(core_, items[i])) {
            cb->Add(s, 0);
            s->Release();
        }
        if (i > 0 && util::Lower(langCodes_[i]) == util::Lower(selected)) sel = (int)i;
    }
    SetSel(IDC_LANG, sel);
}

void SettingsPage::UpdateEnabled() {
    const bool custom = GetCheck(IDC_CUSTOMAPP);
    for (int id : {IDC_CLIENTIDLABEL, IDC_CLIENTID, IDC_OPENPORTAL}) SetVisible(id, custom);
    const bool link = GetCheck(IDC_TITLELINK);
    SetEnabled(IDC_TITLELINKCUSTOM, link);
    SetEnabled(IDC_TITLELINKURL, link && GetCheck(IDC_TITLELINKCUSTOM));
    const bool upd = GetCheck(IDC_UPDCHECK);
    SetEnabled(IDC_UPDFREQ, upd);
    SetEnabled(IDC_UPDAUTO, upd);
}

void SettingsPage::ActionInfo(const char* key) {
    SetText(IDC_ACTIONINFO, key ? T(key) : std::wstring());
    actionUntil_ = key ? util::TickMs() + 15000 : 0;
}

void SettingsPage::OnControlChanged(int id) {
    switch (id) {   // actions, not settings: they never enable AIMP's "Apply" button
        case IDC_CLEARCACHE:
            Worker().ClearCoverCache();
            SetText(IDC_CACHEINFO, T("Cov.Cleared"));
            return;
        case IDC_TEST:
            Worker().SendTest();
            reconnectFrom_ = -1;
            ActionInfo("Adv.TestSent");
            return;
        case IDC_RECONNECT:
            reconnectFrom_ = Worker().Status().connects;
            Worker().Reconnect();
            ActionInfo("Adv.Reconnecting");
            actionUntil_ = util::TickMs() + 30000;   // "Connected." replaces it as soon as the connection is back
            return;
        case IDC_EXPORT:   ExportSettings(); return;
        case IDC_IMPORT:   ImportSettings(); return;
        case IDC_CHECKNOW: update::Check(); return;
        case IDC_INSTALL:  update::Install(); return;
        default: break;
    }
    if (loading_) return;
    UpdateEnabled();
    previewDirty_ = true;
    if (IAIMPUIControl* c = Item(IDC_PREVIEW)) c->Invalidate();   // the live preview follows every change
    if (onModified_) onModified_();                              // enables AIMP's "Apply" button
}

void SettingsPage::Load(const Config* from) {
    if (!form_) return;
    loading_ = true;
    const Config c = from ? *from : config::Get();

    SetCheck(IDC_ENABLE, c.enabled);
    SetSel(IDC_TYPE, c.activityType == 0 ? 1 : 0);
    SetSel(IDC_STATUSDISP, c.statusDisplay);
    SetCheck(IDC_TIMESTAMPS, c.showTimestamps);
    SetSel(IDC_PAUSED, c.pausedBehavior == 0 ? 0 : 1);
    SetText(IDC_PAUSEDMIN, std::to_wstring(c.clearAfterPaused));
    SetCheck(IDC_HIDESTREAMS, c.hideStreams);
    SetText(IDC_EXCLUDE, c.excludePaths);

    SetText(IDC_DETAILS, c.details);
    SetText(IDC_STATE, c.state);
    SetText(IDC_LARGETEXT, c.largeText);
    SetText(IDC_SMALLTEXT, c.smallText);
    SetCheck(IDC_SMALLICON, c.showSmallIcon);
    SetText(IDC_BARLEN, std::to_wstring(c.barLength));
    SetText(IDC_REFRESH, std::to_wstring(c.refreshSeconds));
    SetCheck(IDC_TITLELINK, c.titleLink);
    SetCheck(IDC_TITLELINKCUSTOM, c.titleLinkCustom);
    SetText(IDC_TITLELINKURL, c.titleLinkUrl);

    SetCheck(IDC_COVER, c.coverEnabled);
    SetCheck(IDC_EMBEDDED, c.srcEmbedded);
    SetCheck(IDC_FOLDER, c.srcFolder);
    SetText(IDC_FOLDERNAMES, c.coverNames);
    SetSel(IDC_UPLOADHOST, c.uploadHost);
    SetText(IDC_IMGURID, c.imgurClientId);
    SetCheck(IDC_PREFERLOCAL, c.preferLocal);
    SetCheck(IDC_SPOTIFY, c.srcSpotify);
    SetText(IDC_SPOTIFYID, c.spotifyId);
    SetText(IDC_SPOTIFYSECRET, c.spotifySecret);
    SetCheck(IDC_DEEZER, c.srcDeezer);
    SetCheck(IDC_ITUNES, c.srcItunes);
    SetCheck(IDC_BANDCAMP, c.srcBandcamp);
    SetCheck(IDC_DISCOGS, c.srcDiscogs);
    SetText(IDC_DISCOGSTOKEN, c.discogsToken);
    SetCheck(IDC_MUSICBRAINZ, c.srcMusicBrainz);

    SetText(IDC_EXCLUDEPL, c.excludePlaylists);
    FillLanguages(c.language);
    SetCheck(IDC_CUSTOMAPP, c.useCustomApp);
    SetText(IDC_CLIENTID, c.customClientId);

    SetCheck(IDC_UPDCHECK, c.updateCheck);
    SetSel(IDC_UPDFREQ, c.updateFrequency);
    SetCheck(IDC_UPDAUTO, c.updateAuto);
    UpdateEnabled();

    loading_ = false;
    previewDirty_ = true;
    jobs::FetchDiscordAssets(config::Get().clientId);   // logo / play / pause pictures of the Discord application
    Refresh();
}

void SettingsPage::Collect(Config& c) const {
    c.enabled          = GetCheck(IDC_ENABLE);
    c.activityType     = (GetSel(IDC_TYPE) == 1) ? 0 : 2;
    c.statusDisplay    = std::max(0, std::min(2, GetSel(IDC_STATUSDISP)));
    c.showTimestamps   = GetCheck(IDC_TIMESTAMPS);
    c.pausedBehavior   = (GetSel(IDC_PAUSED) == 0) ? 0 : 1;
    c.clearAfterPaused = std::max(0, GetInt(IDC_PAUSEDMIN, 0));
    c.hideStreams      = GetCheck(IDC_HIDESTREAMS);
    c.excludePaths     = GetText(IDC_EXCLUDE);

    c.details         = GetText(IDC_DETAILS);
    c.state           = GetText(IDC_STATE);
    c.largeText       = GetText(IDC_LARGETEXT);
    c.smallText       = GetText(IDC_SMALLTEXT);
    c.showSmallIcon   = GetCheck(IDC_SMALLICON);
    c.barLength       = std::max(4, std::min(30, GetInt(IDC_BARLEN, 12)));
    c.refreshSeconds  = std::max(5, GetInt(IDC_REFRESH, 15));
    c.titleLink       = GetCheck(IDC_TITLELINK);
    c.titleLinkCustom = GetCheck(IDC_TITLELINKCUSTOM);
    c.titleLinkUrl    = util::Trim(GetText(IDC_TITLELINKURL));
    if (c.titleLinkUrl.empty()) c.titleLinkUrl = kDefaultTitleLink;

    c.coverEnabled   = GetCheck(IDC_COVER);
    c.srcEmbedded    = GetCheck(IDC_EMBEDDED);
    c.srcFolder      = GetCheck(IDC_FOLDER);
    c.coverNames     = GetText(IDC_FOLDERNAMES);
    c.uploadHost     = std::max(0, std::min(3, GetSel(IDC_UPLOADHOST)));
    c.imgurClientId  = util::Trim(GetText(IDC_IMGURID));
    c.preferLocal    = GetCheck(IDC_PREFERLOCAL);
    c.srcSpotify     = GetCheck(IDC_SPOTIFY);
    c.spotifyId      = util::Trim(GetText(IDC_SPOTIFYID));
    c.spotifySecret  = util::Trim(GetText(IDC_SPOTIFYSECRET));
    c.srcDeezer      = GetCheck(IDC_DEEZER);
    c.srcItunes      = GetCheck(IDC_ITUNES);
    c.srcBandcamp    = GetCheck(IDC_BANDCAMP);
    c.srcDiscogs     = GetCheck(IDC_DISCOGS);
    c.discogsToken   = util::Trim(GetText(IDC_DISCOGSTOKEN));
    c.srcMusicBrainz = GetCheck(IDC_MUSICBRAINZ);

    c.excludePlaylists = GetText(IDC_EXCLUDEPL);
    const int lang     = GetSel(IDC_LANG);
    c.language         = (lang > 0 && lang < (int)langCodes_.size()) ? langCodes_[lang] : std::wstring();
    c.useCustomApp     = GetCheck(IDC_CUSTOMAPP);
    c.customClientId   = util::Trim(GetText(IDC_CLIENTID));

    c.updateCheck     = GetCheck(IDC_UPDCHECK);
    c.updateFrequency = std::max(0, std::min(3, GetSel(IDC_UPDFREQ)));
    c.updateAuto      = GetCheck(IDC_UPDAUTO);
}

void SettingsPage::Save() {
    if (!form_) return;
    const std::wstring oldLanguage = config::Get().language;
    config::Update([this](Config& c) { Collect(c); });
    const Config c = config::Get();
    Worker().Refresh();
    jobs::FetchDiscordAssets(c.clientId);
    if (c.language != oldLanguage) {
        i18n::SetOverride(c.language);
        Localize();
    }
}

void SettingsPage::Localize() {
    if (!form_) return;
    loading_ = true;
    for (const TextRef& t : texts_) aimp::SetPropString(core_, t.control, t.prop, T(t.key));
    for (auto& kv : comboKeys_) {
        IAIMPUIControl* c = Item(kv.first);
        if (!c) continue;
        const int sel = GetSel(kv.first);
        IAIMPUIComboBox* cb = static_cast<IAIMPUIComboBox*>(c);
        cb->Clear();
        for (const char* key : kv.second) {
            if (IAIMPString* s = aimp::MakeString(core_, T(key))) {
                cb->Add(s, 0);
                s->Release();
            }
        }
        SetSel(kv.first, sel);
    }
    const int lang = GetSel(IDC_LANG);
    FillLanguages(lang > 0 && lang < (int)langCodes_.size() ? langCodes_[lang] : std::wstring());
    SetText(IDC_ABOUTVERSION, util::Subst(T("About.Version"), AIMP_DISCORD_RPC_VERSION_W, Platform()));
    SetText(IDC_CHANGELOG, ChangelogText());
    for (int id : {IDC_CACHEINFO, IDC_ACTIONINFO, IDC_IOINFO}) SetText(id, std::wstring());
    loading_ = false;
    statusShown_ = false;   // dynamic texts: build them again
    shownCoverText_.clear();
    shownUpdate_.clear();
    previewDirty_ = true;
    Refresh();
}

// ---- export / import (Advanced tab)

#ifdef _WIN32
// The usual Windows "Save as" / "Open" dialog. AIMP's dialog service (IAIMPUIFileDialogs) showed nothing in AIMP
// on Windows, so the Windows dialog is used there (Linux: AIMP's service).
static std::wstring WindowsFileDialog(HWND page, bool save, const std::wstring& title, const std::wstring& type) {
    std::wstring filter = type + L" (*.ini)";
    filter += L'\0';
    filter += L"*.ini";
    filter += L'\0';
    filter += L'\0';
    wchar_t file[MAX_PATH * 2] = L"DiscordRPC-settings.ini";
    if (!save) file[0] = 0;
    const std::wstring dir = config::DataDir();
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = page ? GetAncestor(page, GA_ROOT) : nullptr;   // AIMP's Preferences window
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    ofn.lpstrInitialDir = dir.c_str();
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrDefExt = L"ini";
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
    // the "Open" dialog changes AIMP's current folder despite OFN_NOCHANGEDIR: put it back afterwards
    wchar_t cwd[MAX_PATH * 2] = {0};
    const DWORD cwdLen = GetCurrentDirectoryW(MAX_PATH * 2, cwd);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    const DWORD err = ok ? 0 : CommDlgExtendedError();
    if (cwdLen && cwdLen < MAX_PATH * 2) SetCurrentDirectoryW(cwd);
    if (err) util::Log(L"File dialog failed (error %lu)", err);
    return ok ? std::wstring(file) : std::wstring();
}
#endif

std::wstring SettingsPage::AskFile(bool save) {
#ifdef _WIN32
    return WindowsFileDialog(hwnd_, save, T(save ? "Adv.ExportTitle" : "Adv.ImportTitle"), T("Adv.FileType"));
#else
    IAIMPUIFileDialogs* dlg = nullptr;
    // (some AIMP builds want no owner form for dialogs)
    if ((FAILED(ui_->CreateObject(form_, nullptr, IID_IAIMPUIFileDialogs, reinterpret_cast<void**>(&dlg))) || !dlg) &&
        (FAILED(ui_->CreateObject(nullptr, nullptr, IID_IAIMPUIFileDialogs, reinterpret_cast<void**>(&dlg))) || !dlg)) {
        util::Log(L"AIMP's file dialog is not available");
        return std::wstring();
    }
    IAIMPString* caption = aimp::MakeString(core_, T(save ? "Adv.ExportTitle" : "Adv.ImportTitle"));
    IAIMPString* filter = aimp::MakeString(core_, T("Adv.FileType") + L" (*.ini)|*.ini");
    IAIMPString* name = save ? aimp::MakeString(core_, L"DiscordRPC-settings.ini") : nullptr;   // suggested name
    HRESULT hr;
    if (save) {
        INT32 index = 0;
        hr = dlg->ExecuteSaveDialog(hwnd_, caption, filter, &name, &index);
    } else {
        hr = dlg->ExecuteOpenDialog(hwnd_, caption, filter, &name);
    }
    std::wstring path = (SUCCEEDED(hr) && name) ? aimp::StringOf(name) : std::wstring();
    for (IAIMPString* s : {caption, filter, name})
        if (s) s->Release();
    dlg->Release();
    return util::Trim(path);
#endif
}

void SettingsPage::ExportSettings() {
    std::wstring path = AskFile(true);
    if (path.empty()) return;
    if (util::FileExt(path) != L"ini") path += L".ini";
    Config c = config::Get();
    Collect(c);   // what the page shows, also changes that were not applied yet
    SetText(IDC_IOINFO, util::Subst(T(config::ExportTo(path, c) ? "Adv.Exported" : "Adv.ExportFailed"), path));
}

void SettingsPage::ImportSettings() {
    const std::wstring path = AskFile(false);
    if (path.empty()) return;
    if (!config::ImportFrom(path)) {
        SetText(IDC_IOINFO, util::Subst(T("Adv.ImportFailed"), path));
        return;
    }
    i18n::SetOverride(config::Get().language);
    Load();
    Localize();
    Worker().Refresh();
    SetText(IDC_IOINFO, util::Subst(T("Adv.Imported"), path));
}

// ---- refresh (about every 500 ms on Windows; on player events and background results on Linux)

void SettingsPage::Refresh() {
    if (!form_) return;
    const PresenceStatus st = Worker().Status();
    const Snapshot s = Worker().LastSnapshot();
    const Config c = config::Get();
    if (reconnectFrom_ >= 0 && st.connects > reconnectFrom_) {   // the reconnect is done
        reconnectFrom_ = -1;
        ActionInfo("Adv.Reconnected");
        actionUntil_ = util::TickMs() + 5000;
    }
    if (actionUntil_ && util::TickMs() >= actionUntil_) {
        reconnectFrom_ = -1;
        ActionInfo(nullptr);
    }
    RefreshStatus(st, c);
    RefreshCover(st, s, c);
    RefreshImages(st);
    RefreshUpdate();
    RefreshLog();

    // live preview: new track / play state, or the progress bar moves on
    if (s.trackId != previewTrack_ || s.state != previewState_) {
        previewTrack_ = s.trackId;
        previewState_ = s.state;
        previewDirty_ = true;
    }
    const int tab = ActiveTab();
    if (previewDirty_ || (s.state == PlayState::Playing && (tab == kTabDisplay || tab < 0))) {
        previewDirty_ = false;
        if (IAIMPUIControl* pb = Item(IDC_PREVIEW)) pb->Invalidate();
    }
}

void SettingsPage::RefreshStatus(const PresenceStatus& st, const Config& c) {
    if (statusShown_ && st == shownStatus_) return;
    statusShown_ = true;
    shownStatus_ = st;
    const std::wstring status = util::Subst(T("Gen.Status"), StatusText(st));
    SetText(IDC_STATUS, status);
    SetText(IDC_CONN, st.lastSent ? status + L"   " + util::Subst(T("Adv.LastUpdate"), util::FormatClock(st.lastSent))
                                  : status);
    const std::wstring& id = st.clientId.empty() ? c.clientId : st.clientId;   // empty before the first presence round
    SetText(IDC_TRANSPORT,
            util::Subst(T("Adv.Channel"), st.endpoint.empty() ? T("Adv.NoChannel") : util::FromUtf8(st.endpoint)) +
                L"   " + util::Subst(T("Adv.AppInUse"), id, T(id == kDefaultClientId ? "Adv.AppBuiltin" : "Adv.AppOwn")));
    previewDirty_ = true;   // user name / avatar
}

void SettingsPage::RefreshCover(const PresenceStatus& st, const Snapshot& s, const Config& c) {
    std::wstring text;
    if (!c.coverEnabled)                    text = T("Cov.Off");
    else if (s.state == PlayState::Stopped) text = T("Cov.Nothing");
    else if (st.coverSearching)             text = T("Cov.Searching");
    else if (st.coverUrl.empty())           text = T("Cov.None");
    else                                    text = util::Subst(T("Cov.Source"), SourceText(st.coverSource));
    if (text != shownCoverText_) {
        shownCoverText_ = text;
        SetText(IDC_COVERINFO, text);
    }
    const std::wstring url = util::FromUtf8(st.coverUrl);
    if (url == shownCoverUrl_) return;
    shownCoverUrl_ = url;
    SetUrl(IDC_COVERLINK, url);
    SetVisible(IDC_COVERLINK, !url.empty());
    IAIMPImage*& img = images_["cover"];
    if (img) img->Release();
    img = nullptr;
    SetImage(IDC_COVERIMG, nullptr);
    if (!url.empty()) jobs::FetchImage("cover", url, std::wstring(), 0);
    previewDirty_ = true;
}

void SettingsPage::RefreshImages(const PresenceStatus& st) {
    // the user's Discord avatar for the member list in the preview
    if (OnlyChars(st.user.id, "0123456789") && OnlyChars(st.user.avatar, "0123456789abcdef_")) {
        const std::wstring key = util::FromUtf8(st.user.id + "_" + st.user.avatar);
        if (key != userAvatarKey_) {
            userAvatarKey_ = key;
            jobs::FetchImage("user", L"https://cdn.discordapp.com/avatars/" + util::FromUtf8(st.user.id) + L"/" +
                                         util::FromUtf8(st.user.avatar) + L".png?size=64",
                             L"user_" + key + L".png", 30);
        }
    }
    for (const char* key : {"cover", "author", "user", "asset:aimp", "asset:play", "asset:pause"}) {
        std::string bytes;
        if (!jobs::TakeImage(key, bytes)) continue;
        IAIMPImage*& img = images_[key];
        if (img) img->Release();
        img = MakeImage(bytes);
        const std::string k = key;
        if (k == "cover") SetImage(IDC_COVERIMG, img);
        if (k == "author")
            if (IAIMPUIControl* a = Item(IDC_AVATAR)) a->Invalidate();
        previewDirty_ = true;
    }
}

void SettingsPage::RefreshUpdate() {
    const update::State u = update::Get();
    const Config c = config::Get();
    const std::wstring checked = c.updateLastCheck ? util::FormatDateTime(c.updateLastCheck) : std::wstring();
    std::wstring text, version = u.version;
    bool install = false;
    switch (u.phase) {
        case update::Phase::Checking:    text = T("Upd.Checking"); break;
        case update::Phase::Available:   text = util::Subst(T("Upd.Available"), u.version); install = true; break;
        case update::Phase::Downloading: text = util::Subst(T("Upd.Downloading"), u.version); break;
        case update::Phase::Opened:      text = util::Subst(T("Upd.Opened"), u.version); break;
        case update::Phase::Restarting:  text = util::Subst(T("Upd.Restarting"), u.version); break;
        case update::Phase::Installed:   text = util::Subst(T("Upd.RestartManual"), u.version); break;
        case update::Phase::Failed:      text = util::Subst(T("Upd.Failed"), u.detail); break;
        case update::Phase::UpToDate:    text = util::Subst(T("Upd.Latest"), checked); break;
        default:
            if (!c.updateLatest.empty() && util::CompareVersions(c.updateLatest, AIMP_DISCORD_RPC_VERSION_W) > 0) {
                version = c.updateLatest;   // found by an earlier check
                text = util::Subst(T("Upd.Available"), version);
                install = true;
            } else {
                text = checked.empty() ? T("Upd.Never") : util::Subst(T("Upd.Latest"), checked);
            }
    }
    const std::wstring key = text + L"|" + (install ? version : std::wstring()) + L"|" + u.detail;
    if (key == shownUpdate_) return;
    shownUpdate_ = key;
    SetText(IDC_UPDSTATUS, text);
    SetText(IDC_INSTALL, util::Subst(T("About.Install"), version));
    SetVisible(IDC_INSTALL, install);
    SetVisible(IDC_NOTES, install);
    const bool opened = u.phase == update::Phase::Opened && !u.detail.empty();
    SetUrl(IDC_PKGFOLDER, opened ? util::FileUrl(util::DirName(u.detail)) : std::wstring());
    SetVisible(IDC_PKGFOLDER, opened);
}

void SettingsPage::RefreshLog() {
    IAIMPUIControl* c = Item(IDC_LOG);
    const uint64_t seq = util::LogSeq();
    if (!c || seq == logSeq_) return;
    const size_t count = logSeq_ == 0 ? 60 : (size_t)std::min<uint64_t>(seq - logSeq_, 60);
    for (const std::wstring& line : util::RecentLog(count, &logSeq_)) {
        if (IAIMPString* s = aimp::MakeString(core_, line)) {
            static_cast<IAIMPUIMemo*>(c)->AddLine(s);
            s->Release();
        }
    }
}

// ---- live preview (Display tab)

void SettingsPage::BuildPreview() {
    Config c = config::Get();
    Collect(c);   // what the page shows, also before "Apply"
    config::Resolve(c);
    Snapshot s = Worker().LastSnapshot();
    const bool example = s.state == PlayState::Stopped || (s.track.title.empty() && s.track.fileName.empty());
    if (example) {   // nothing is playing: show an example track
        s = Snapshot();
        s.state = PlayState::Playing;
        s.position = 83;
        s.duration = 354;
        s.track.artist = L"Queen";
        s.track.title = L"Bohemian Rhapsody";
        s.track.album = L"A Night at the Opera";
        s.track.year = L"1975";
        s.track.genre = L"Rock";
        s.track.trackNumber = L"11";
        s.track.fileName = L"Queen - Bohemian Rhapsody.flac";
        s.track.playlist = L"Favourites";
    }
    const auto pausedFor = s.state == PlayState::Paused ? std::chrono::steady_clock::now() - s.stamp
                                                        : std::chrono::steady_clock::duration::zero();
    const ActivityTexts t = ComputeTexts(c, s, pausedFor);
    const PresenceStatus st = Worker().Status();

    PreviewData& d = preview_;
    d = PreviewData();
    d.hidden = t.hidden != Hidden::No;
    d.message = d.hidden ? HiddenText(t.hidden) : (example ? T("Prev.Example") : std::wstring());
    std::wstring app = c.useCustomApp ? jobs::AppName(c.clientId) : std::wstring(L"AIMP");
    if (app.empty()) app = T("Prev.YourApp");
    d.header = util::Subst(T(c.activityType == 0 ? "Prev.Playing" : "Prev.Listening"), app);
    d.details = util::FromUtf8(t.details);
    d.state = util::FromUtf8(t.state);
    d.largeText = util::FromUtf8(t.largeText);
    d.memberLine = c.statusDisplay == 1 ? d.state : c.statusDisplay == 2 ? d.details : d.header;
    const std::wstring user = util::FromUtf8(st.user.globalName.empty() ? st.user.name : st.user.globalName);
    d.userName = user.empty() ? T("Prev.You") : user;
    if (c.showSmallIcon && !t.smallText.empty()) d.iconTip = util::Subst(T("Prev.IconTip"), util::FromUtf8(t.smallText));
    if (!t.detailsUrl.empty()) {
        std::wstring url = util::FromUtf8(t.detailsUrl);
        for (const wchar_t* prefix : {L"https://", L"http://", L"www."})
            if (url.rfind(prefix, 0) == 0) url = url.substr(wcslen(prefix));
        d.linkTip = util::Subst(T("Prev.Link"), url);
    } else {
        d.linkTip = T("Prev.NoLink");
    }
    d.progress = c.showTimestamps && t.playing && c.activityType == 2;
    if (c.showTimestamps && t.playing && c.activityType == 0)
        d.elapsed = util::Subst(T("Prev.Elapsed"), util::FormatTime(t.pos));
    d.pos = t.pos;
    d.dur = t.dur;
    d.playing = t.playing;
    d.smallIcon = c.showSmallIcon;
    auto image = [this](const char* key) -> IAIMPImage* {
        auto it = images_.find(key);
        return it == images_.end() ? nullptr : it->second;
    };
    d.cover = (!example && c.coverEnabled && image("cover")) ? image("cover") : image("asset:aimp");
    d.icon = image(t.playing ? "asset:play" : "asset:pause");
    d.avatar = image("user");
}

void SettingsPage::OnDraw(int id, HCANVAS canvas, const RECT& r) {
    if (id == IDC_AVATAR) {
        auto it = images_.find("author");
        if (it != images_.end() && it->second) DrawRoundedImage(canvas, r, it->second, 10);
        return;
    }
    BuildPreview();
    DrawPreview(canvas, r, preview_);
}
