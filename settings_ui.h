// The settings tab inside AIMP's preferences, built with AIMP's own UI API (apiGUI.h / IAIMPServiceUI).
// The same code runs on Windows and Linux; AIMP draws the controls, so they follow the current skin.
// All methods must be called on AIMP's main thread.
#pragma once
#include "aimp_util.h"
#include "apiGUI.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>
#ifndef _WIN32
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#include "config.h"
#include "presence.h"
#include "preview.h"

// The presets changed (saved / deleted on the Templates tab): the plugin renames its hot key actions (plugin.cpp).
void PresetsChanged();

class SettingsPage {
public:
    // Creates the page as a child form of 'parent' (AIMP's options container). nullptr if the UI service is missing.
    static SettingsPage* Create(IAIMPCore* core, HWND parent, std::function<void()> onModified);

    HWND Hwnd() const { return hwnd_; }
    void Load(const Config* from = nullptr);   // config (or given defaults) -> controls
    void Save();                               // controls -> config, applies immediately
    void Refresh();                            // status lines, previews, update state (cheap if nothing changed)
    void Localize();                           // language changed: set all texts again
    void Destroy();                            // destroys the form and deletes the object

    void OnControlChanged(int id);             // called by the controls' event handlers
    void OnDraw(int id, HCANVAS canvas, const RECT& r);   // paint boxes: live preview, author picture
    bool OnWheel(int id, int delta);           // mouse wheel over a scroll box (AIMP 4) or a list in one: true = handled
    void OnMouse(int id, int what, int x, int y);   // placeholder buttons, author picture: 0 move, 1 down, 2 up, 3 left
    void TickFx();                             // author picture: next frame of "boop!" / "pat" (while one runs)
    bool ScrollStep(int box, int delta);       // scrolls a scroll box by about two rows

private:
    SettingsPage() = default;
    bool Build(HWND parent);
    void BuildGeneral(IAIMPUIWinControl* page);
    void BuildDisplay(IAIMPUIWinControl* page);
    void BuildTemplates(IAIMPUIWinControl* page);
    void BuildCover(IAIMPUIWinControl* page);
    void BuildSources(IAIMPUIWinControl* page);
    void BuildAdvanced(IAIMPUIWinControl* page);
    void BuildAbout(IAIMPUIWinControl* page);
    void UpdateEnabled();
    void Collect(Config& c) const;             // page values -> config fields (the rest stays)
    void FillLanguages(const std::wstring& selected);
    void RefreshStatus(const PresenceStatus& st, const Config& c);
    void RefreshCover(const PresenceStatus& st, const Snapshot& s, const Config& c);
    void RefreshImages(const PresenceStatus& st);
    void RefreshUpdate();
    void RefreshLog();
    void BuildPreview(bool templatesTab);
    void ShowTemplateSet(int set);            // the texts of one set (normal / paused / stream) -> multi-line fields
    void FillPresets(int select);             // preset list from the config, 'select' = index or -1
    void SavePresetSlot();
    void DeletePresetSlot();
    void LoadPresetSlot(int index);
    int  PresetSlot() const;                  // selected slot, else the first free one
    void ExportSettings();
    void ImportSettings();
    std::wstring AskFile(bool save);

    // control factory (coordinates in pixels at 96 DPI, relative to the tab sheet; texts are i18n keys)
    IAIMPUIControl* Create(IAIMPUIWinControl* parent, int id, REFIID iid, bool events, bool mouse = false);
    void Place(IAIMPUIControl* c, int x, int y, int w, int h);
    void Text(IAIMPUIControl* c, int prop, const char* key);   // sets the text and remembers it for Localize
    void Label(IAIMPUIWinControl* p, const char* key, int x, int y, int w, int h = 18, int id = 0);
    void Link(IAIMPUIWinControl* p, const char* key, const std::wstring& url, int x, int y, int w, int id = 0);
    void Check(IAIMPUIWinControl* p, int id, const char* key, int x, int y, int w);
    void Edit(IAIMPUIWinControl* p, int id, int x, int y, int w, bool password = false);
    void Combo(IAIMPUIWinControl* p, int id, std::initializer_list<const char*> keys, int x, int y, int w);
    void Button(IAIMPUIWinControl* p, int id, const char* key, int x, int y, int w);
    void Memo(IAIMPUIWinControl* p, int id, int x, int y, int w, int h, bool editable = false);
    void Image(IAIMPUIWinControl* p, int id, int x, int y, int w, int h);
    void Paint(IAIMPUIWinControl* p, int id, int x, int y, int w, int h, bool mouse = false);
    void InsertPlaceholder(int box, const std::wstring& placeholder);
    void ActionInfo(const char* key);         // note next to the Advanced tab's buttons, cleared after a while
    IAIMPUIWinControl* BeginScroll(IAIMPUIWinControl* page, int id, int height);   // settings that scroll (or page)
    void EndScroll();
    void FillRulePresets();                   // preset lists of the automatic rules ("-", "1: name", ...)

    IAIMPUIControl* Item(int id) const;
    void SetCheck(int id, bool v);
    bool GetCheck(int id) const;
    bool Focused(int id) const;
    void SetText(int id, const std::wstring& s);
    std::wstring GetText(int id) const;
    void SetSel(int id, int index);
    int  GetSel(int id) const;
    int  GetInt(int id, int def) const;
    void SetVisible(int id, bool v);
    void SetEnabled(int id, bool v);
    void SetUrl(int id, const std::wstring& url);
    void SetImage(int id, IAIMPImage* img);
    IAIMPImage* MakeImage(const std::string& bytes);   // new reference or nullptr
    int  ActiveTab() const;

    struct TextRef { IAIMPUIControl* control; int prop; const char* key; };

    IAIMPCore*         core_ = nullptr;      // not owned (the plugin keeps it alive)
    IAIMPServiceUI*    ui_ = nullptr;
    IAIMPUIForm*       form_ = nullptr;
    IUnknown*          formEvents_ = nullptr;
    IAIMPUIPageControl* tabs_ = nullptr;     // owned through anonymous_
    IAIMPUITabSheet*   templatesSheet_ = nullptr;   // shown only with advanced placeholders (owned through anonymous_)
    HWND               hwnd_ = 0;
    std::map<int, IAIMPUIControl*> items_;   // id -> control (one reference each)
    std::map<int, int> kinds_;               // id -> control kind
    std::vector<IAIMPUIControl*> anonymous_; // controls without id (one reference each)
    std::vector<IUnknown*> handlers_;        // event handlers (one reference each)
    std::vector<TextRef> texts_;             // texts to set again when the language changes
    std::map<int, std::vector<const char*>> comboKeys_;
    std::vector<std::wstring> langCodes_;    // language combo: "" = automatic
    std::map<std::string, IAIMPImage*> images_;   // "cover", "author", "user", "asset:aimp", ... (owned)
    int  nextName_ = 0;
    bool loading_ = false;
    std::function<void()> onModified_;

    // what is shown now (Refresh only touches controls whose content changed)
    PresenceStatus shownStatus_;
    bool           statusShown_ = false;
    std::wstring   shownCoverUrl_, shownCoverText_, shownUpdate_, userAvatarKey_, assetsFor_;
    uint64_t       logSeq_ = 0, previewTrack_ = 0;
    PlayState      previewState_ = PlayState::Stopped;
    bool           previewDirty_ = true;
    uint64_t       actionUntil_ = 0;          // TickMs when the action note is cleared (0 = none)
    int            reconnectFrom_ = -1;       // connects before "Reconnect" was pressed (-1 = none pending)
    PreviewData    preview_;

    // Templates tab: the texts of the three sets (one line each, as stored); the fields show set tplSet_
    std::wstring   tpl_[3][4];
    int            tplSet_ = 0;
    int            loadedPreset_ = 0;         // preset loaded into the page and not applied yet (1..10, 0 = none)
    std::wstring   shownRule_;                // text of the "rule in use" line

    // placeholder bars (paint boxes): group tabs, then the placeholders of the selected group (design units)
    struct ChipGroup { const char* key; std::vector<std::wstring> items; };
    struct ChipBox {
        std::vector<ChipGroup> groups;
        std::vector<Chip> chips;   // the tabs (one per group), then the placeholders of group 'group'
        float width = 445;
        int group = 0, hover = -1, pressed = -1;
    };
    std::map<int, ChipBox> chips_;
    float LayoutChips(ChipBox& b);            // fills b.chips, returns the height
    int BarHeight(ChipBox& b);
    void PlaceholderBar(IAIMPUIWinControl* page, int id, ChipBox box, int y, int h);
    int            lastField_[2] = {0, 0};    // field the buttons insert into: Display tab, Templates tab

    // About tab: "boop!" / "pat" over the author's picture (kAvatarSpots in settings_ui.cpp)
    void AvatarMouse(int what, int x, int y);
    void StartFx();
    void StopFx(bool quit);
    int            fxSpot_ = -1, fxPressed_ = -1;   // area whose word is shown / where the button went down (-1 = none)
    uint64_t       fxStart_ = 0;              // TickMs of the click
#ifdef _WIN32
    UINT_PTR       fxTimer_ = 0;
#else
    std::thread    fxThread_;                 // started with the first click, ends with the page
    std::mutex     fxMu_;
    std::condition_variable fxCv_;
    bool           fxRun_ = false, fxQuit_ = false;
#endif

    // scroll boxes: AIMP 4 does not scroll them with the mouse wheel, the page does (rows: bounds of the controls)
    struct ScrollRow { IAIMPUIControl* control; int top, bottom; };
    struct ScrollArea { int height = 0, content = 0, top = 0; IAIMPUIControl* panel = nullptr; std::vector<ScrollRow> rows; };
    std::map<int, ScrollArea> scrolls_;
    ScrollArea*    building_ = nullptr;       // controls placed now go into this scroll box
};
