#include "settings_ui.h"

#include <algorithm>
#include <atomic>
#include <cwchar>

#include "presence.h"
#include "version.h"

using aimp::ComPtr;
using aimp::SameIID;

namespace {

enum Ids {
    // General
    IDC_ENABLE = 200, IDC_CUSTOMAPP, IDC_CLIENTID, IDC_OPENPORTAL, IDC_CLIENTIDLABEL, IDC_TYPE, IDC_STATUSDISP,
    IDC_TIMESTAMPS, IDC_PAUSED, IDC_PAUSEDMIN, IDC_HIDESTREAMS, IDC_EXCLUDE, IDC_STATUS,
    // Display
    IDC_DETAILS = 300, IDC_STATE, IDC_LARGETEXT, IDC_SMALLTEXT, IDC_SMALLICON, IDC_BARLEN, IDC_REFRESH,
    // Cover
    IDC_COVER = 400, IDC_EMBEDDED, IDC_FOLDER, IDC_FOLDERNAMES, IDC_UPLOADHOST, IDC_IMGURID, IDC_PREFERLOCAL,
    IDC_CLEARCACHE, IDC_CACHEINFO,
    // Links
    IDC_TITLELINK = 500, IDC_TITLELINKCUSTOM, IDC_TITLELINKURL, IDC_TITLELINKURLLABEL,
    // Online sources
    IDC_SPOTIFY = 600, IDC_SPOTIFYID, IDC_SPOTIFYSECRET, IDC_DEEZER, IDC_ITUNES, IDC_BANDCAMP, IDC_DISCOGS,
    IDC_DISCOGSTOKEN, IDC_MUSICBRAINZ,
};

enum Kind { kLabel, kCheck, kEdit, kCombo, kButton };

const int kRowH = 22;   // height of edits / combos

// Forwards IAIMPUIChangeEvents (check box toggled, text edited, item selected, button clicked) of one control.
class ChangeHandler final : public IAIMPUIChangeEvents {
public:
    ChangeHandler(SettingsPage* page, int id) : page_(page), id_(id) {}

    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (SameIID(riid, IID_IUnknown) || SameIID(riid, IID_IAIMPUIChangeEvents)) {
            *ppv = static_cast<IAIMPUIChangeEvents*>(this);
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
    void WINAPI OnChanged(IUnknown*) override {
        if (page_) page_->OnControlChanged(id_);
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

const int kPages = 5;
const wchar_t* const kTabNames[kPages] = {L"General", L"Display", L"Cover art", L"Online sources", L"Links"};

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
    IAIMPUIPageControl* tabs = nullptr;
    name = aimp::MakeString(core_, L"Tabs");
    hr = ui_->CreateControl(form_, form_, name, nullptr, IID_IAIMPUIPageControl, reinterpret_cast<void**>(&tabs));
    if (name) name->Release();
    if (FAILED(hr) || !tabs) return false;
    TAIMPUIControlPlacement pl = {};
    pl.Alignment = ualClient;
    tabs->SetPlacement(pl);
    anonymous_.push_back(tabs);

    IAIMPUITabSheet* sheets[kPages] = {};
    for (int i = 0; i < kPages; ++i) {
        name = aimp::MakeString(core_, L"Page" + std::to_wstring(i));
        hr = tabs->Add(name, &sheets[i]);
        if (name) name->Release();
        if (FAILED(hr) || !sheets[i]) return false;
        aimp::SetPropString(core_, sheets[i], AIMPUI_TABSHEET_PROPID_CAPTION, kTabNames[i]);
        anonymous_.push_back(sheets[i]);
    }
    BuildGeneral(sheets[0]);
    BuildDisplay(sheets[1]);
    BuildCover(sheets[2]);
    BuildSources(sheets[3]);
    BuildLinks(sheets[4]);
    tabs->SetValueAsInt32(AIMPUI_PAGECONTROL_PROPID_ACTIVE, 0);

    hwnd_ = form_->GetHandle();
    return true;
}

void SettingsPage::Destroy() {
    for (IUnknown* h : handlers_) {
        static_cast<ChangeHandler*>(static_cast<IAIMPUIChangeEvents*>(h))->Detach();
        h->Release();
    }
    handlers_.clear();
    for (auto& kv : items_) kv.second->Release();
    items_.clear();
    for (IAIMPUIControl* c : anonymous_) c->Release();
    anonymous_.clear();
    if (form_) {
        form_->Destroy(0);
        form_->Release();
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
    delete this;
}

// ================================================================================ control factory

IAIMPUIControl* SettingsPage::Create(IAIMPUIWinControl* parent, int id, REFIID iid, bool events) {
    IUnknown* handler = nullptr;
    if (events) {
        handler = new ChangeHandler(this, id);
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

void SettingsPage::Label(IAIMPUIWinControl* p, const wchar_t* text, int x, int y, int w, int h, int id) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUILabel, false);
    if (!c) return;
    if (id) kinds_[id] = kLabel;
    c->SetValueAsInt32(AIMPUI_LABEL_PROPID_AUTOSIZE, 0);
    c->SetValueAsInt32(AIMPUI_LABEL_PROPID_WORDWRAP, h > 20 ? 1 : 0);
    aimp::SetPropString(core_, c, AIMPUI_LABEL_PROPID_TEXT, text);
    Place(c, x, y, w, h);
}

void SettingsPage::Link(IAIMPUIWinControl* p, const wchar_t* text, const wchar_t* url, int x, int y, int w, int id) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUILabel, false);
    if (!c) return;
    if (id) kinds_[id] = kLabel;
    c->SetValueAsInt32(AIMPUI_LABEL_PROPID_AUTOSIZE, 0);
    aimp::SetPropString(core_, c, AIMPUI_LABEL_PROPID_TEXT, text);
    aimp::SetPropString(core_, c, AIMPUI_LABEL_PROPID_URL, url);   // AIMP opens it in the browser on click
    Place(c, x, y, w, 18);
}

void SettingsPage::Check(IAIMPUIWinControl* p, int id, const wchar_t* text, int x, int y, int w) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUICheckBox, true);
    if (!c) return;
    kinds_[id] = kCheck;
    c->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_AUTOSIZE, 0);
    aimp::SetPropString(core_, c, AIMPUI_CHECKBOX_PROPID_CAPTION, text);
    Place(c, x, y, w, 20);
}

void SettingsPage::Edit(IAIMPUIWinControl* p, int id, int x, int y, int w, bool password) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIEdit, true);
    if (!c) return;
    kinds_[id] = kEdit;
    if (password) c->SetValueAsInt32(AIMPUI_EDIT_PROPID_PASSWORDCHAR, '*');
    Place(c, x, y, w, kRowH);
}

void SettingsPage::Combo(IAIMPUIWinControl* p, int id, std::initializer_list<const wchar_t*> items, int x, int y,
                         int w) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIComboBox, true);
    if (!c) return;
    kinds_[id] = kCombo;
    c->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_STYLE, AIMPUI_COMBOBOX_STYLE_LIST);
    IAIMPUIComboBox* cb = static_cast<IAIMPUIComboBox*>(c);
    for (const wchar_t* item : items) {
        IAIMPString* s = aimp::MakeString(core_, item);
        if (!s) continue;
        cb->Add(s, 0);
        s->Release();
    }
    Place(c, x, y, w, kRowH);
}

void SettingsPage::Button(IAIMPUIWinControl* p, int id, const wchar_t* text, int x, int y, int w) {
    IAIMPUIControl* c = Create(p, id, IID_IAIMPUIButton, true);
    if (!c) return;
    kinds_[id] = kButton;
    aimp::SetPropString(core_, c, AIMPUI_BUTTON_PROPID_CAPTION, text);
    Place(c, x, y, w, 25);
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
    int prop = (k != kinds_.end() && k->second == kLabel) ? AIMPUI_LABEL_PROPID_TEXT : AIMPUI_BASEEDIT_PROPID_TEXT;
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

// ================================================================================ layout

void SettingsPage::BuildGeneral(IAIMPUIWinControl* p) {
    int y = 8;
    Check(p, IDC_ENABLE, L"Enable Discord Rich Presence", 10, y, 470);                       y += 28;
    Label(p, L"Activity type:", 10, y + 3, 190);
    Combo(p, IDC_TYPE, {L"Listening (shows a progress bar)", L"Playing (elapsed time only)"}, 205, y, 265); y += 30;
    Label(p, L"Discord status text shows:", 10, y + 3, 190);
    Combo(p, IDC_STATUSDISP, {L"Application name", L"Artist (state line)", L"Track title (details line)"}, 205, y,
          265);                                                                                  y += 32;
    Check(p, IDC_TIMESTAMPS, L"Show progress bar / elapsed time", 10, y, 470);               y += 26;
    Label(p, L"When playback is paused:", 10, y + 3, 190);
    Combo(p, IDC_PAUSED, {L"Show \"Paused\" status", L"Hide (PreMiD / other activity shows)"}, 205, y, 265); y += 30;
    Label(p, L"Clear presence after pause for (min, 0 = never):", 10, y + 3, 290);
    Edit(p, IDC_PAUSEDMIN, 305, y, 60);                                                      y += 30;
    Check(p, IDC_HIDESTREAMS, L"Hide presence for internet radio / streams", 10, y, 470);    y += 26;
    Label(p, L"Hide presence when the file path contains (separate with ;):", 10, y, 470);   y += 20;
    Edit(p, IDC_EXCLUDE, 10, y, 460);                                                        y += 32;
    Label(p, L"Status: ...", 10, y, 470, 34, IDC_STATUS);                                    y += 42;
    // advanced: own Discord application (normally not needed - everyone uses the built-in one)
    Check(p, IDC_CUSTOMAPP, L"Advanced: use my own Discord application", 10, y, 470);         y += 26;
    Label(p, L"Application ID:", 28, y + 3, 120, 18, IDC_CLIENTIDLABEL);
    Edit(p, IDC_CLIENTID, 155, y, 200);
    Link(p, L"Developer Portal", L"https://discord.com/developers/applications", 365, y + 3, 110, IDC_OPENPORTAL);
    y += 36;
    Label(p, L"AIMP Discord Rich Presence " AIMP_DISCORD_RPC_VERSION_W L" by Fl4sh", 10, y, 470);
}

void SettingsPage::BuildDisplay(IAIMPUIWinControl* p) {
    int y = 8;
    Label(p, L"Placeholders: %artist% %title% %album% %albumartist% %genre% %year% %track% %filename% %ext% "
             L"%pos% %dur% %percent% %bar% %status%", 10, y, 470, 34);                       y += 42;
    Label(p, L"Details line (first line):", 10, y + 3, 190);       Edit(p, IDC_DETAILS, 205, y, 265);   y += 30;
    Label(p, L"State line (second line):", 10, y + 3, 190);        Edit(p, IDC_STATE, 205, y, 265);     y += 30;
    Label(p, L"Cover tooltip:", 10, y + 3, 190);                   Edit(p, IDC_LARGETEXT, 205, y, 265); y += 30;
    Label(p, L"Small icon tooltip:", 10, y + 3, 190);              Edit(p, IDC_SMALLTEXT, 205, y, 265); y += 34;
    Check(p, IDC_SMALLICON, L"Show play / pause icon (small image)", 10, y, 470);            y += 26;
    Label(p, L"Text progress bar length (4-30):", 10, y + 3, 190); Edit(p, IDC_BARLEN, 205, y, 60);     y += 30;
    Label(p, L"Refresh interval in seconds (min 5):", 10, y + 3, 190); Edit(p, IDC_REFRESH, 205, y, 60); y += 34;
    Label(p, L"%bar% / %pos% / %percent% are static text and only refresh at the interval above. The native "
             L"Discord progress bar (Listening type) updates itself and needs no refresh.", 10, y, 470, 48);
}

void SettingsPage::BuildCover(IAIMPUIWinControl* p) {
    int y = 8;
    Check(p, IDC_COVER, L"Show album cover in Discord", 10, y, 470);                         y += 26;
    Label(p, L"Discord can only show covers that have a public URL. The cover of your file can be uploaded "
             L"(catbox.moe needs no account), or it is looked up online (see \"Online sources\"). "
             L"Every cover is resolved once and cached.", 10, y, 470, 50);                   y += 56;
    Check(p, IDC_EMBEDDED, L"Local source: embedded cover in tags (MP3 / FLAC / M4A)", 10, y, 470); y += 24;
    Check(p, IDC_FOLDER, L"Local source: image in the track's folder", 10, y, 470);           y += 24;
    Label(p, L"Folder image names (;):", 28, y + 3, 170);  Edit(p, IDC_FOLDERNAMES, 205, y, 265); y += 34;
    Label(p, L"Upload local covers to:", 10, y + 3, 190);
    Combo(p, IDC_UPLOADHOST, {L"Off (online lookup only)", L"catbox.moe (no account needed)",
                              L"Imgur (needs Client-ID)"}, 205, y, 265);                     y += 30;
    Label(p, L"Imgur Client-ID:", 28, y + 3, 170);         Edit(p, IDC_IMGURID, 205, y, 265);  y += 30;
    Check(p, IDC_PREFERLOCAL, L"Prefer the file's own cover (online lookup only if it has none)", 10, y, 470); y += 24;
    Label(p, L"Uploaded covers are reachable by anyone who has the link.", 28, y, 440);     y += 28;
    Button(p, IDC_CLEARCACHE, L"Clear cover cache", 10, y, 170);
    Label(p, L"", 190, y + 4, 280, 18, IDC_CACHEINFO);
}

void SettingsPage::BuildSources(IAIMPUIWinControl* p) {
    int y = 8;
    Label(p, L"Online lookups search the artist / album and use the public image URL directly - nothing is "
             L"uploaded. Sources are tried top to bottom, the first hit wins.", 10, y, 470, 34);  y += 42;
    Check(p, IDC_SPOTIFY, L"Spotify (needs a free developer app)", 10, y, 300);
    Link(p, L"Create app", L"https://developer.spotify.com/dashboard", 330, y + 1, 140);           y += 28;
    Label(p, L"Client ID:", 28, y + 3, 120);      Edit(p, IDC_SPOTIFYID, 155, y, 315);             y += 28;
    Label(p, L"Client Secret:", 28, y + 3, 120);  Edit(p, IDC_SPOTIFYSECRET, 155, y, 315, true);   y += 34;
    Check(p, IDC_DEEZER, L"Deezer (no key)", 10, y, 470);                                          y += 24;
    Check(p, IDC_ITUNES, L"Apple Music / iTunes (no key)", 10, y, 470);                            y += 24;
    Check(p, IDC_BANDCAMP, L"Bandcamp (no key, unofficial endpoint)", 10, y, 470);                 y += 28;
    Check(p, IDC_DISCOGS, L"Discogs (needs a free personal access token)", 10, y, 300);
    Link(p, L"Get token", L"https://www.discogs.com/settings/developers", 330, y + 1, 140);        y += 28;
    Label(p, L"Token:", 28, y + 3, 120);          Edit(p, IDC_DISCOGSTOKEN, 155, y, 315, true);    y += 34;
    Check(p, IDC_MUSICBRAINZ, L"MusicBrainz + Cover Art Archive (no key)", 10, y, 470);
}

void SettingsPage::BuildLinks(IAIMPUIWinControl* p) {
    int y = 8;
    Check(p, IDC_TITLELINK, L"Make the song title clickable in Discord", 10, y, 470);           y += 24;
    Label(p, L"By default a click on the title opens a YouTube search for artist + title.", 28, y, 440); y += 32;
    Check(p, IDC_TITLELINKCUSTOM, L"Use my own link instead", 28, y, 440);                        y += 26;
    Label(p, L"URL:", 46, y + 3, 50, 18, IDC_TITLELINKURLLABEL);
    Edit(p, IDC_TITLELINKURL, 100, y, 370);                                                      y += 32;
    Label(p, L"Placeholders: %artist% %title% %album% %albumartist% %year% - they are URL-encoded "
             L"automatically. Example: https://music.youtube.com/search?q=%artist%+%title%", 46, y, 424, 48);
}

// ================================================================================ behaviour

void SettingsPage::UpdateStatus() {
    PresenceStatus st = Worker().Status();
    std::wstring text = L"Status: ";
    if (st.connected) text += L"connected to Discord" + (st.user.empty() ? std::wstring() : L" as " + st.user);
    else              text += st.message.empty() ? L"waiting..." : st.message;
    if (text != lastStatus_) {
        lastStatus_ = text;
        SetText(IDC_STATUS, text);
    }
}

void SettingsPage::UpdateEnabled() {
    const bool custom = GetCheck(IDC_CUSTOMAPP);
    for (int id : {IDC_CLIENTIDLABEL, IDC_CLIENTID, IDC_OPENPORTAL}) SetVisible(id, custom);
    const bool link = GetCheck(IDC_TITLELINK);
    const bool own  = GetCheck(IDC_TITLELINKCUSTOM);
    SetEnabled(IDC_TITLELINKCUSTOM, link);
    SetEnabled(IDC_TITLELINKURL, link && own);
    SetEnabled(IDC_TITLELINKURLLABEL, link && own);
}

void SettingsPage::OnControlChanged(int id) {
    if (id == IDC_CLEARCACHE) {
        Worker().ClearCoverCache();
        SetText(IDC_CACHEINFO, L"Cover cache cleared.");
        return;
    }
    if (loading_) return;
    if (id == IDC_CUSTOMAPP || id == IDC_TITLELINK || id == IDC_TITLELINKCUSTOM) UpdateEnabled();
    if (onModified_) onModified_();   // enables AIMP's "Apply" button
}

void SettingsPage::Load(const Config* from) {
    if (!form_) return;
    loading_ = true;
    Config c = from ? *from : config::Get();

    SetCheck(IDC_ENABLE, c.enabled);
    SetCheck(IDC_CUSTOMAPP, c.useCustomApp);
    SetText(IDC_CLIENTID, c.customClientId);
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

    SetCheck(IDC_TITLELINK, c.titleLink);
    SetCheck(IDC_TITLELINKCUSTOM, c.titleLinkCustom);
    SetText(IDC_TITLELINKURL, c.titleLinkUrl);
    UpdateEnabled();

    loading_ = false;
    UpdateStatus();
}

void SettingsPage::Save() {
    if (!form_) return;
    Config c = config::Get();

    c.enabled          = GetCheck(IDC_ENABLE);
    c.useCustomApp     = GetCheck(IDC_CUSTOMAPP);
    c.customClientId   = util::Trim(GetText(IDC_CLIENTID));
    c.activityType     = (GetSel(IDC_TYPE) == 1) ? 0 : 2;
    c.statusDisplay    = std::max(0, std::min(2, GetSel(IDC_STATUSDISP)));
    c.showTimestamps   = GetCheck(IDC_TIMESTAMPS);
    c.pausedBehavior   = (GetSel(IDC_PAUSED) == 0) ? 0 : 1;
    c.clearAfterPaused = std::max(0, GetInt(IDC_PAUSEDMIN, 0));
    c.hideStreams      = GetCheck(IDC_HIDESTREAMS);
    c.excludePaths     = GetText(IDC_EXCLUDE);

    c.details        = GetText(IDC_DETAILS);
    c.state          = GetText(IDC_STATE);
    c.largeText      = GetText(IDC_LARGETEXT);
    c.smallText      = GetText(IDC_SMALLTEXT);
    c.showSmallIcon  = GetCheck(IDC_SMALLICON);
    c.barLength      = std::max(4, std::min(30, GetInt(IDC_BARLEN, 12)));
    c.refreshSeconds = std::max(5, GetInt(IDC_REFRESH, 15));

    c.coverEnabled  = GetCheck(IDC_COVER);
    c.srcEmbedded   = GetCheck(IDC_EMBEDDED);
    c.srcFolder     = GetCheck(IDC_FOLDER);
    c.coverNames    = GetText(IDC_FOLDERNAMES);
    c.uploadHost    = std::max(0, std::min(2, GetSel(IDC_UPLOADHOST)));
    c.imgurClientId = util::Trim(GetText(IDC_IMGURID));
    c.preferLocal   = GetCheck(IDC_PREFERLOCAL);
    c.srcSpotify     = GetCheck(IDC_SPOTIFY);
    c.spotifyId      = util::Trim(GetText(IDC_SPOTIFYID));
    c.spotifySecret  = util::Trim(GetText(IDC_SPOTIFYSECRET));
    c.srcDeezer      = GetCheck(IDC_DEEZER);
    c.srcItunes      = GetCheck(IDC_ITUNES);
    c.srcBandcamp    = GetCheck(IDC_BANDCAMP);
    c.srcDiscogs     = GetCheck(IDC_DISCOGS);
    c.discogsToken   = util::Trim(GetText(IDC_DISCOGSTOKEN));
    c.srcMusicBrainz = GetCheck(IDC_MUSICBRAINZ);

    c.titleLink       = GetCheck(IDC_TITLELINK);
    c.titleLinkCustom = GetCheck(IDC_TITLELINKCUSTOM);
    c.titleLinkUrl    = util::Trim(GetText(IDC_TITLELINKURL));

    config::Set(c);
    Worker().Refresh();
}
