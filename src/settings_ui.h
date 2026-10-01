// The settings tab inside AIMP's preferences, built with AIMP's own UI API (apiGUI.h / IAIMPServiceUI).
// The same code runs on Windows and Linux; AIMP draws the controls, so they follow the current skin.
// All methods must be called on AIMP's main thread.
#pragma once
#include "aimp_util.h"
#include "apiGUI.h"

#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "config.h"

class SettingsPage {
public:
    // Creates the page as a child form of 'parent' (AIMP's options container). nullptr if the UI service is missing.
    static SettingsPage* Create(IAIMPCore* core, HWND parent, std::function<void()> onModified);

    HWND Hwnd() const { return hwnd_; }
    void Load(const Config* from = nullptr);   // config (or given defaults) -> controls
    void Save();                               // controls -> config, applies immediately
    void UpdateStatus();                       // refresh the connection status line
    void Destroy();                            // destroys the form and deletes the object

    void OnControlChanged(int id);             // called by the controls' event handlers

private:
    SettingsPage() = default;
    bool Build(HWND parent);
    void BuildGeneral(IAIMPUIWinControl* page);
    void BuildDisplay(IAIMPUIWinControl* page);
    void BuildCover(IAIMPUIWinControl* page);
    void BuildSources(IAIMPUIWinControl* page);
    void BuildLinks(IAIMPUIWinControl* page);
    void UpdateEnabled();

    // control factory (coordinates in pixels at 96 DPI, relative to the tab sheet)
    IAIMPUIControl* Create(IAIMPUIWinControl* parent, int id, REFIID iid, bool events);
    void Place(IAIMPUIControl* c, int x, int y, int w, int h);
    void Label(IAIMPUIWinControl* p, const wchar_t* text, int x, int y, int w, int h = 18, int id = 0);
    void Link(IAIMPUIWinControl* p, const wchar_t* text, const wchar_t* url, int x, int y, int w, int id = 0);
    void Check(IAIMPUIWinControl* p, int id, const wchar_t* text, int x, int y, int w);
    void Edit(IAIMPUIWinControl* p, int id, int x, int y, int w, bool password = false);
    void Combo(IAIMPUIWinControl* p, int id, std::initializer_list<const wchar_t*> items, int x, int y, int w);
    void Button(IAIMPUIWinControl* p, int id, const wchar_t* text, int x, int y, int w);

    IAIMPUIControl* Item(int id) const;
    void SetCheck(int id, bool v);
    bool GetCheck(int id) const;
    void SetText(int id, const std::wstring& s);
    std::wstring GetText(int id) const;
    void SetSel(int id, int index);
    int  GetSel(int id) const;
    int  GetInt(int id, int def) const;
    void SetVisible(int id, bool v);
    void SetEnabled(int id, bool v);

    IAIMPCore*         core_ = nullptr;      // not owned (the plugin keeps it alive)
    IAIMPServiceUI*    ui_ = nullptr;
    IAIMPUIForm*       form_ = nullptr;
    IUnknown*          formEvents_ = nullptr;
    HWND               hwnd_ = 0;
    std::map<int, IAIMPUIControl*> items_;   // id -> control (one reference each)
    std::map<int, int> kinds_;               // id -> control kind
    std::vector<IAIMPUIControl*> anonymous_; // labels without id (one reference each)
    std::vector<IUnknown*> handlers_;        // event handlers (one reference each)
    int  nextName_ = 0;
    bool loading_ = false;
    std::wstring lastStatus_;
    std::function<void()> onModified_;
};
