#include "settings_ui.h"

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <initializer_list>

#include "config.h"
#include "presence.h"
#include "util.h"

namespace {

enum Ids {
    IDC_TAB = 100,
    // General
    IDC_ENABLE = 200, IDC_CLIENTID, IDC_OPENPORTAL, IDC_TYPE, IDC_STATUSDISP, IDC_TIMESTAMPS, IDC_PAUSED,
    IDC_PAUSEDMIN, IDC_HIDESTREAMS, IDC_EXCLUDE, IDC_STATUS,
    // Display
    IDC_DETAILS = 300, IDC_STATE, IDC_LARGETEXT, IDC_SMALLTEXT, IDC_SMALLICON, IDC_PLAYKEY, IDC_PAUSEKEY,
    IDC_BARLEN, IDC_REFRESH,
    // Cover
    IDC_COVER = 400, IDC_EMBEDDED, IDC_FOLDER, IDC_FOLDERNAMES, IDC_UPLOADHOST, IDC_IMGURID, IDC_PREFERLOCAL,
    IDC_FALLBACK, IDC_CLEARCACHE,
    // Online sources
    IDC_SPOTIFY = 600, IDC_SPOTIFYID, IDC_SPOTIFYSECRET, IDC_DEEZER, IDC_ITUNES, IDC_BANDCAMP, IDC_DISCOGS,
    IDC_DISCOGSTOKEN, IDC_MUSICBRAINZ, IDC_OPENSPOTIFY, IDC_OPENDISCOGS,
    // Buttons
    IDC_BTN1 = 500, IDC_BTN1LABEL, IDC_BTN1URL, IDC_BTN2, IDC_BTN2LABEL, IDC_BTN2URL,
};

const UINT_PTR TIMER_STATUS = 1;
const int kPages = 5;
const wchar_t* kTabNames[kPages] = {L"General", L"Display", L"Cover art", L"Online sources", L"Buttons"};

}  // namespace

// ================================================================================ creation

HWND SettingsPage::CreateChildDialog(HWND parent, DLGPROC proc, LPARAM param, bool visible) {
    // Empty in-memory dialog template: DLGTEMPLATE + menu/class/title (all zero). Controls are added in code.
    alignas(4) WORD tmpl[12] = {0};
    DWORD style = WS_CHILD | WS_CLIPCHILDREN | DS_CONTROL | (visible ? WS_VISIBLE : 0);
    DWORD ex = WS_EX_CONTROLPARENT;
    tmpl[0] = LOWORD(style); tmpl[1] = HIWORD(style);
    tmpl[2] = LOWORD(ex);    tmpl[3] = HIWORD(ex);
    tmpl[4] = 0;             // no template controls
    tmpl[7] = 100; tmpl[8] = 100;  // cx, cy (resized later)
    return CreateDialogIndirectParamW(g_hModule, reinterpret_cast<LPCDLGTEMPLATEW>(tmpl), parent, proc, param);
}

SettingsPage* SettingsPage::Create(HWND parent, std::function<void()> onModified) {
    SettingsPage* p = new SettingsPage();
    p->onModified_ = std::move(onModified);
    HWND h = CreateChildDialog(parent, &SettingsPage::MainProc, reinterpret_cast<LPARAM>(p), true);
    if (!h) {
        delete p;
        return nullptr;
    }
    RECT rc;
    GetClientRect(parent, &rc);
    MoveWindow(h, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    p->Load();
    return p;
}

void SettingsPage::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    delete this;
}

INT_PTR CALLBACK SettingsPage::MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SettingsPage* self = reinterpret_cast<SettingsPage*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
        case WM_INITDIALOG:
            self = reinterpret_cast<SettingsPage*>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = h;
            self->BuildUi();
            return FALSE;
        case WM_SIZE:
            if (self) self->Layout();
            return TRUE;
        case WM_NOTIFY:
            if (self && reinterpret_cast<NMHDR*>(l)->idFrom == IDC_TAB &&
                reinterpret_cast<NMHDR*>(l)->code == TCN_SELCHANGE) {
                self->ShowPage(TabCtrl_GetCurSel(self->tab_));
                return TRUE;
            }
            return FALSE;
        case WM_COMMAND:
            if (self) self->OnCommand(w, l);
            return TRUE;
        case WM_TIMER:
            if (self && w == TIMER_STATUS) self->UpdateStatus();
            return TRUE;
        case WM_DESTROY:
            if (self) {
                KillTimer(h, TIMER_STATUS);
                if (self->font_) { DeleteObject(self->font_); self->font_ = nullptr; }
            }
            return FALSE;
    }
    return FALSE;
}

INT_PTR CALLBACK SettingsPage::PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND) {  // bubble up to the main dialog
        SendMessageW(GetParent(h), WM_COMMAND, w, l);
        return TRUE;
    }
    return FALSE;
}

// ================================================================================ helpers

int SettingsPage::S(int v) const { return MulDiv(v, dpi_, 96); }

HWND SettingsPage::Ctl(HWND page, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int x, int y,
                       int w, int h, int id) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), page,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hModule, nullptr);
    if (c && font_) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return c;
}
HWND SettingsPage::Label(HWND page, const wchar_t* text, int x, int y, int w, int h, int id) {
    return Ctl(page, L"STATIC", text, SS_LEFT, 0, x, y, w, h, id);
}
HWND SettingsPage::Check(HWND page, int id, const wchar_t* text, int x, int y, int w) {
    return Ctl(page, L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, 0, x, y, w, 18, id);
}
HWND SettingsPage::Edit(HWND page, int id, int x, int y, int w) {
    return Ctl(page, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, x, y, w, 22, id);
}
HWND SettingsPage::Combo(HWND page, int id, std::initializer_list<const wchar_t*> items, int x, int y, int w) {
    HWND c = Ctl(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 0, x, y, w, 200, id);
    for (const wchar_t* s : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    return c;
}
HWND SettingsPage::PushButton(HWND page, int id, const wchar_t* text, int x, int y, int w) {
    return Ctl(page, L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP, 0, x, y, w, 24, id);
}

HWND SettingsPage::Item(int id) const {
    for (HWND p : pages_) {
        if (!p) continue;
        HWND c = GetDlgItem(p, id);
        if (c) return c;
    }
    return nullptr;
}
void SettingsPage::SetCheck(int id, bool v) { SendMessageW(Item(id), BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }
bool SettingsPage::GetCheck(int id) const { return SendMessageW(Item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void SettingsPage::SetText(int id, const std::wstring& s) { SetWindowTextW(Item(id), s.c_str()); }
std::wstring SettingsPage::GetText(int id) const {
    HWND h = Item(id);
    int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, L'\0');
    int got = GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)got);
    return s;
}
void SettingsPage::SetSel(int id, int index) { SendMessageW(Item(id), CB_SETCURSEL, (WPARAM)index, 0); }
int SettingsPage::GetSel(int id) const { return (int)SendMessageW(Item(id), CB_GETCURSEL, 0, 0); }
int SettingsPage::GetInt(int id, int def) const {
    std::wstring t = util::Trim(GetText(id));
    return t.empty() ? def : _wtoi(t.c_str());
}

// ================================================================================ layout

void SettingsPage::BuildUi() {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);

    HDC dc = GetDC(nullptr);
    dpi_ = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);

    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) font_ = CreateFontIndirectW(&ncm.lfMessageFont);

    tab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 10, 10,
                           hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB)), g_hModule, nullptr);
    if (font_) SendMessageW(tab_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    for (int i = 0; i < kPages; ++i) {
        TCITEMW ti = {};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<LPWSTR>(kTabNames[i]);
        TabCtrl_InsertItem(tab_, i, &ti);
    }
    for (int i = 0; i < kPages; ++i) {
        pages_[i] = CreateChildDialog(hwnd_, &SettingsPage::PageProc, 0, false);
        EnableThemeDialogTexture(pages_[i], ETDT_ENABLETAB);
    }
    BuildGeneral(pages_[0]);
    BuildDisplay(pages_[1]);
    BuildCover(pages_[2]);
    BuildSources(pages_[3]);
    BuildButtons(pages_[4]);

    Layout();
    ShowPage(0);
    SetTimer(hwnd_, TIMER_STATUS, 1000, nullptr);
}

void SettingsPage::Layout() {
    if (!tab_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    MoveWindow(tab_, 0, 0, rc.right, rc.bottom, TRUE);
    RECT r = rc;
    TabCtrl_AdjustRect(tab_, FALSE, &r);
    for (HWND p : pages_)
        if (p) MoveWindow(p, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}

void SettingsPage::ShowPage(int index) {
    for (int i = 0; i < kPages; ++i) ShowWindow(pages_[i], i == index ? SW_SHOW : SW_HIDE);
}

void SettingsPage::BuildGeneral(HWND p) {
    int y = 8;
    Check(p, IDC_ENABLE, L"Enable Discord Rich Presence", 10, y, 470);                       y += 26;
    Label(p, L"Discord Application ID (Client ID):", 10, y, 320);                            y += 18;
    Edit(p, IDC_CLIENTID, 10, y, 250);
    PushButton(p, IDC_OPENPORTAL, L"Open Developer Portal", 270, y - 1, 170);                y += 28;
    Label(p, L"Create a free application in the Discord Developer Portal. Its name is what Discord shows "
             L"(e.g. \"Listening to AIMP\"). Optional art assets (fallback logo, play/pause icons) are uploaded "
             L"there under Rich Presence > Art Assets.", 10, y, 470, 44);                    y += 50;

    Label(p, L"Activity type:", 10, y + 3, 190);
    Combo(p, IDC_TYPE, {L"Listening (shows a progress bar)", L"Playing (elapsed time only)"}, 205, y, 265); y += 28;
    Label(p, L"Discord status text shows:", 10, y + 3, 190);
    Combo(p, IDC_STATUSDISP, {L"Application name", L"Artist (state line)", L"Track title (details line)"}, 205, y, 265); y += 30;

    Check(p, IDC_TIMESTAMPS, L"Show progress bar / elapsed time", 10, y, 470);               y += 24;
    Label(p, L"When playback is paused:", 10, y + 3, 190);
    Combo(p, IDC_PAUSED, {L"Show \"Paused\" status", L"Clear presence"}, 205, y, 265);       y += 28;
    Label(p, L"Clear presence after pause for (min, 0 = never):", 10, y + 3, 290);
    Edit(p, IDC_PAUSEDMIN, 305, y, 60);                                                      y += 28;
    Check(p, IDC_HIDESTREAMS, L"Hide presence for internet radio / streams", 10, y, 470);    y += 24;
    Label(p, L"Hide presence when the file path contains (separate with ;):", 10, y, 470);   y += 18;
    Edit(p, IDC_EXCLUDE, 10, y, 460);                                                        y += 30;
    Ctl(p, L"STATIC", L"Status: ...", SS_LEFT, 0, 10, y, 470, 32, IDC_STATUS);               y += 36;
    Label(p, L"AIMP Discord Rich Presence 1.1.0", 10, y, 470);
}

void SettingsPage::BuildDisplay(HWND p) {
    int y = 8;
    Label(p, L"Placeholders: %artist% %title% %album% %albumartist% %genre% %year% %track% %filename% %ext% "
             L"%pos% %dur% %percent% %bar% %status%", 10, y, 470, 32);                       y += 40;
    Label(p, L"Details line (first line):", 10, y + 3, 190);       Edit(p, IDC_DETAILS, 205, y, 265);   y += 28;
    Label(p, L"State line (second line):", 10, y + 3, 190);        Edit(p, IDC_STATE, 205, y, 265);     y += 28;
    Label(p, L"Cover tooltip:", 10, y + 3, 190);                   Edit(p, IDC_LARGETEXT, 205, y, 265); y += 28;
    Label(p, L"Small icon tooltip:", 10, y + 3, 190);              Edit(p, IDC_SMALLTEXT, 205, y, 265); y += 32;
    Check(p, IDC_SMALLICON, L"Show play / pause icon (small image)", 10, y, 470);            y += 24;
    Label(p, L"Asset key for \"playing\" icon:", 10, y + 3, 190);  Edit(p, IDC_PLAYKEY, 205, y, 150);   y += 28;
    Label(p, L"Asset key for \"paused\" icon:", 10, y + 3, 190);   Edit(p, IDC_PAUSEKEY, 205, y, 150);  y += 32;
    Label(p, L"Text progress bar length (4-30):", 10, y + 3, 190); Edit(p, IDC_BARLEN, 205, y, 60);     y += 28;
    Label(p, L"Refresh interval in seconds (min 5):", 10, y + 3, 190); Edit(p, IDC_REFRESH, 205, y, 60); y += 32;
    Label(p, L"%bar% / %pos% / %percent% are static text and only refresh at the interval above. The native "
             L"Discord progress bar (Listening type) updates itself and needs no refresh.", 10, y, 470, 44);
}

void SettingsPage::BuildCover(HWND p) {
    int y = 8;
    Check(p, IDC_COVER, L"Show album cover in Discord", 10, y, 470);                         y += 24;
    Label(p, L"Discord can only show covers that have a public URL. The cover of your file can be uploaded "
             L"(catbox.moe needs no account), or it is looked up online (see \"Online sources\"). "
             L"Every cover is resolved once and cached.", 10, y, 470, 44);                   y += 50;
    Check(p, IDC_EMBEDDED, L"Local source: embedded cover in tags (MP3 / FLAC / M4A)", 10, y, 470); y += 22;
    Check(p, IDC_FOLDER, L"Local source: image in the track's folder", 10, y, 470);           y += 22;
    Label(p, L"Folder image names (;):", 28, y + 3, 170);  Edit(p, IDC_FOLDERNAMES, 205, y, 265); y += 32;
    Label(p, L"Upload local covers to:", 10, y + 3, 190);
    Combo(p, IDC_UPLOADHOST, {L"Off (online lookup only)", L"catbox.moe (no account needed)",
                              L"Imgur (needs Client-ID)"}, 205, y, 265);                     y += 28;
    Label(p, L"Imgur Client-ID:", 28, y + 3, 170);         Edit(p, IDC_IMGURID, 205, y, 265);  y += 28;
    Check(p, IDC_PREFERLOCAL, L"Prefer the file's own cover (online lookup only if it has none)", 10, y, 470); y += 22;
    Label(p, L"Uploaded covers are reachable by anyone who has the link.", 28, y, 440);     y += 26;
    Label(p, L"Fallback asset key:", 10, y + 3, 190);      Edit(p, IDC_FALLBACK, 205, y, 150); y += 32;
    PushButton(p, IDC_CLEARCACHE, L"Clear cover cache", 10, y, 170);
}

void SettingsPage::BuildSources(HWND p) {
    int y = 8;
    Label(p, L"Online lookups search the artist / album and use the public image URL directly - nothing is "
             L"uploaded. Sources are tried top to bottom, the first hit wins.", 10, y, 470, 32);  y += 40;
    Check(p, IDC_SPOTIFY, L"Spotify (needs a free developer app)", 10, y, 300);
    PushButton(p, IDC_OPENSPOTIFY, L"Create app", 330, y - 3, 140);                              y += 26;
    Label(p, L"Client ID:", 28, y + 3, 120);      Edit(p, IDC_SPOTIFYID, 155, y, 315);             y += 26;
    Label(p, L"Client Secret:", 28, y + 3, 120);  SendMessageW(Edit(p, IDC_SPOTIFYSECRET, 155, y, 315), EM_SETPASSWORDCHAR, L'*', 0);         y += 32;
    Check(p, IDC_DEEZER, L"Deezer (no key)", 10, y, 470);                                          y += 22;
    Check(p, IDC_ITUNES, L"Apple Music / iTunes (no key)", 10, y, 470);                            y += 22;
    Check(p, IDC_BANDCAMP, L"Bandcamp (no key, unofficial endpoint)", 10, y, 470);                 y += 26;
    Check(p, IDC_DISCOGS, L"Discogs (needs a free personal access token)", 10, y, 300);
    PushButton(p, IDC_OPENDISCOGS, L"Get token", 330, y - 3, 140);                                y += 26;
    Label(p, L"Token:", 28, y + 3, 120);          SendMessageW(Edit(p, IDC_DISCOGSTOKEN, 155, y, 315), EM_SETPASSWORDCHAR, L'*', 0);          y += 32;
    Check(p, IDC_MUSICBRAINZ, L"MusicBrainz + Cover Art Archive (no key)", 10, y, 470);
}

void SettingsPage::BuildButtons(HWND p) {
    int y = 8;
    Label(p, L"Discord shows up to two buttons. Buttons are not visible on your own profile, only to others. "
             L"Placeholders in URLs are URL-encoded automatically.", 10, y, 470, 32);        y += 40;
    Check(p, IDC_BTN1, L"Button 1", 10, y, 470);                                              y += 24;
    Label(p, L"Label:", 28, y + 3, 80);  Edit(p, IDC_BTN1LABEL, 110, y, 360);                 y += 26;
    Label(p, L"URL:", 28, y + 3, 80);    Edit(p, IDC_BTN1URL, 110, y, 360);                   y += 36;
    Check(p, IDC_BTN2, L"Button 2", 10, y, 470);                                              y += 24;
    Label(p, L"Label:", 28, y + 3, 80);  Edit(p, IDC_BTN2LABEL, 110, y, 360);                 y += 26;
    Label(p, L"URL:", 28, y + 3, 80);    Edit(p, IDC_BTN2URL, 110, y, 360);
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

void SettingsPage::OnCommand(WPARAM w, LPARAM) {
    int id = LOWORD(w), code = HIWORD(w);
    if (id == IDC_OPENPORTAL && code == BN_CLICKED) {
        ShellExecuteW(hwnd_, L"open", L"https://discord.com/developers/applications", nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == IDC_OPENSPOTIFY && code == BN_CLICKED) {
        ShellExecuteW(hwnd_, L"open", L"https://developer.spotify.com/dashboard", nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == IDC_OPENDISCOGS && code == BN_CLICKED) {
        ShellExecuteW(hwnd_, L"open", L"https://www.discogs.com/settings/developers", nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == IDC_CLEARCACHE && code == BN_CLICKED) {
        Worker().ClearCoverCache();
        MessageBoxW(hwnd_, L"Cover cache cleared.", L"Discord Rich Presence", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (loading_ || !onModified_) return;
    if (code == EN_CHANGE || code == CBN_SELCHANGE || (code == BN_CLICKED && id != IDC_STATUS)) onModified_();
}

void SettingsPage::Load() {
    if (!hwnd_) return;
    loading_ = true;
    Config c = config::Get();

    SetCheck(IDC_ENABLE, c.enabled);
    SetText(IDC_CLIENTID, c.clientId);
    SetSel(IDC_TYPE, c.activityType == 0 ? 1 : 0);
    SetSel(IDC_STATUSDISP, c.statusDisplay);
    SetCheck(IDC_TIMESTAMPS, c.showTimestamps);
    SetSel(IDC_PAUSED, c.pausedBehavior == 1 ? 1 : 0);
    SetText(IDC_PAUSEDMIN, std::to_wstring(c.clearAfterPaused));
    SetCheck(IDC_HIDESTREAMS, c.hideStreams);
    SetText(IDC_EXCLUDE, c.excludePaths);

    SetText(IDC_DETAILS, c.details);
    SetText(IDC_STATE, c.state);
    SetText(IDC_LARGETEXT, c.largeText);
    SetText(IDC_SMALLTEXT, c.smallText);
    SetCheck(IDC_SMALLICON, c.showSmallIcon);
    SetText(IDC_PLAYKEY, c.playKey);
    SetText(IDC_PAUSEKEY, c.pauseKey);
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
    SetText(IDC_FALLBACK, c.fallbackKey);

    SetCheck(IDC_BTN1, c.btn1Enabled);
    SetText(IDC_BTN1LABEL, c.btn1Label);
    SetText(IDC_BTN1URL, c.btn1Url);
    SetCheck(IDC_BTN2, c.btn2Enabled);
    SetText(IDC_BTN2LABEL, c.btn2Label);
    SetText(IDC_BTN2URL, c.btn2Url);

    loading_ = false;
    UpdateStatus();
}

void SettingsPage::Save() {
    if (!hwnd_) return;
    Config c = config::Get();

    c.enabled          = GetCheck(IDC_ENABLE);
    c.clientId         = util::Trim(GetText(IDC_CLIENTID));
    c.activityType     = (GetSel(IDC_TYPE) == 1) ? 0 : 2;
    c.statusDisplay    = std::max(0, std::min(2, GetSel(IDC_STATUSDISP)));
    c.showTimestamps   = GetCheck(IDC_TIMESTAMPS);
    c.pausedBehavior   = (GetSel(IDC_PAUSED) == 1) ? 1 : 0;
    c.clearAfterPaused = std::max(0, GetInt(IDC_PAUSEDMIN, 0));
    c.hideStreams      = GetCheck(IDC_HIDESTREAMS);
    c.excludePaths     = GetText(IDC_EXCLUDE);

    c.details        = GetText(IDC_DETAILS);
    c.state          = GetText(IDC_STATE);
    c.largeText      = GetText(IDC_LARGETEXT);
    c.smallText      = GetText(IDC_SMALLTEXT);
    c.showSmallIcon  = GetCheck(IDC_SMALLICON);
    c.playKey        = util::Trim(GetText(IDC_PLAYKEY));
    c.pauseKey       = util::Trim(GetText(IDC_PAUSEKEY));
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
    c.fallbackKey   = util::Trim(GetText(IDC_FALLBACK));

    c.btn1Enabled = GetCheck(IDC_BTN1);
    c.btn1Label   = GetText(IDC_BTN1LABEL);
    c.btn1Url     = GetText(IDC_BTN1URL);
    c.btn2Enabled = GetCheck(IDC_BTN2);
    c.btn2Label   = GetText(IDC_BTN2LABEL);
    c.btn2Url     = GetText(IDC_BTN2URL);

    config::Set(c);
    Worker().Refresh();
}
