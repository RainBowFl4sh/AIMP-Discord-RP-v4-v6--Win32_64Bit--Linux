// The settings tab that is embedded into AIMP's options dialog (plain Win32, no AIMP SDK types).
#pragma once
#include <windows.h>

#include <functional>

#include "config.h"
#include <string>

class SettingsPage {
public:
    // Creates the page as a child window of 'parent' (AIMP's options container).
    static SettingsPage* Create(HWND parent, std::function<void()> onModified);

    HWND Hwnd() const { return hwnd_; }
    void Load(const Config* from = nullptr);   // config (or given defaults) -> controls
    void Save();      // controls -> config, applies immediately
    void Destroy();   // destroys the window and deletes the object

private:
    SettingsPage() = default;

    static INT_PTR CALLBACK MainProc(HWND, UINT, WPARAM, LPARAM);
    static INT_PTR CALLBACK PageProc(HWND, UINT, WPARAM, LPARAM);
    static HWND CreateChildDialog(HWND parent, DLGPROC proc, LPARAM param, bool visible);

    void BuildUi();
    void BuildGeneral(HWND page);
    void BuildDisplay(HWND page);
    void BuildCover(HWND page);
    void BuildButtons(HWND page);
    void BuildSources(HWND page);
    void UpdateEnabled();
    void Layout();
    void ShowPage(int index);
    void UpdateStatus();
    void OnCommand(WPARAM w, LPARAM l);

    // control helpers
    int  S(int v) const;
    HWND Ctl(HWND page, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int x, int y, int w, int h, int id);
    HWND Label(HWND page, const wchar_t* text, int x, int y, int w, int h = 16, int id = -1);
    HWND Check(HWND page, int id, const wchar_t* text, int x, int y, int w);
    HWND Edit(HWND page, int id, int x, int y, int w);
    HWND Combo(HWND page, int id, std::initializer_list<const wchar_t*> items, int x, int y, int w);
    HWND PushButton(HWND page, int id, const wchar_t* text, int x, int y, int w);

    HWND Item(int id) const;
    void SetCheck(int id, bool v);
    bool GetCheck(int id) const;
    void SetText(int id, const std::wstring& s);
    std::wstring GetText(int id) const;
    void SetSel(int id, int index);
    int  GetSel(int id) const;
    int  GetInt(int id, int def) const;

    HWND hwnd_ = nullptr;
    HWND tab_ = nullptr;
    HWND pages_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    HFONT font_ = nullptr;
    int dpi_ = 96;
    bool loading_ = false;
    std::wstring lastStatus_;
    std::function<void()> onModified_;
};
