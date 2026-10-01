// Minimal stand-in for AIMP's UI service (apiGUI.h) and options dialog service, used by host_test.cpp to drive
// the plugin's settings page without AIMP: every control simply stores its properties, so the test can read
// what the plugin put into the page, change values and fire the change events like a user would.
#pragma once
#include "apiGUI.h"
#include "apiOptions.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mockui {

using TStr = std::basic_string<TChar>;

inline TStr StrOf(IUnknown* obj) {
    IAIMPString* s = nullptr;
    if (!obj || obj->QueryInterface(IID_IAIMPString, reinterpret_cast<void**>(&s)) != S_OK || !s) return TStr();
    TStr r(s->GetData(), (size_t)s->GetLength());
    s->Release();
    return r;
}

struct Registry;

// One generic object for every control interface: methods an interface does not have are just extra functions.
template <class I>
class Ctl : public I {
public:
    Ctl(Registry* reg, const TStr& kind, const TStr& name, IUnknown* events)
        : kind(kind), name(name), reg_(reg), events_(events) {
        if (events_) events_->AddRef();
    }
    virtual ~Ctl() { if (events_) events_->Release(); }

    TStr kind, name;
    std::map<int, long long> ints;
    std::map<int, TStr> strs;
    std::vector<TStr> items;   // combo box items
    RECT bounds = {0, 0, 0, 0};
    RECT anchors = {0, 0, 0, 0};
    int alignment = 0;
    bool placed = false;

    // test helper: what a user action would trigger
    void FireChanged() {
        IAIMPUIChangeEvents* e = nullptr;
        if (events_ && events_->QueryInterface(IID_IAIMPUIChangeEvents, reinterpret_cast<void**>(&e)) == S_OK && e) {
            e->OnChanged(static_cast<I*>(this));
            e->Release();
        }
    }

    // IUnknown
    HRESULT __unknwncall QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (memcmp(&riid, &IID_IUnknown, sizeof(GUID)) == 0) { *ppv = static_cast<I*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return (DWORD)++ref_; }
    DWORD __unknwncall Release() override {
        DWORD r = (DWORD)--ref_;
        if (r == 0) delete this;
        return r;
    }

    // IAIMPPropertyList
    void WINAPI BeginUpdate() override {}
    void WINAPI EndUpdate() override {}
    HRESULT WINAPI Reset() override { return S_OK; }
    HRESULT WINAPI GetValueAsFloat(INT32 id, DOUBLE* v) override {
        auto it = ints.find(id);
        if (it == ints.end()) return E_FAIL;
        *v = (double)it->second;
        return S_OK;
    }
    HRESULT WINAPI GetValueAsInt32(INT32 id, INT32* v) override {
        auto it = ints.find(id);
        if (it == ints.end()) return E_FAIL;
        *v = (INT32)it->second;
        return S_OK;
    }
    HRESULT WINAPI GetValueAsInt64(INT32 id, INT64* v) override {
        auto it = ints.find(id);
        if (it == ints.end()) return E_FAIL;
        *v = it->second;
        return S_OK;
    }
    HRESULT WINAPI GetValueAsObject(INT32 id, CONSTIID iid, void** v) override;
    HRESULT WINAPI SetValueAsFloat(INT32 id, const DOUBLE v) override { ints[id] = (long long)v; return S_OK; }
    HRESULT WINAPI SetValueAsInt32(INT32 id, INT32 v) override { ints[id] = v; return S_OK; }
    HRESULT WINAPI SetValueAsInt64(INT32 id, const INT64 v) override { ints[id] = v; return S_OK; }
    HRESULT WINAPI SetValueAsObject(INT32 id, IUnknown* v) override { strs[id] = StrOf(v); return S_OK; }

    // IAIMPUIControl
    HRESULT WINAPI GetPlacement(TAIMPUIControlPlacement* p) override { *p = {}; p->Bounds = bounds; return S_OK; }
    HRESULT WINAPI GetPlacementConstraints(TAIMPUIControlPlacementConstraints* c) override { *c = {}; return S_OK; }
    HRESULT WINAPI SetPlacement(TAIMPUIControlPlacement p) override {
        bounds = p.Bounds;
        anchors = p.Anchors;
        alignment = (int)p.Alignment;
        placed = true;
        return S_OK;
    }
    HRESULT WINAPI SetPlacementConstraints(TAIMPUIControlPlacementConstraints) override { return S_OK; }
    HRESULT WINAPI ClientToScreen(POINT*) override { return S_OK; }
    HRESULT WINAPI ScreenToClient(POINT*) override { return S_OK; }
    HRESULT WINAPI PaintTo(HCANVAS, INT32, INT32) override { return S_OK; }
    HRESULT WINAPI Invalidate() override { return S_OK; }

    // IAIMPUIWinControl
    virtual HRESULT WINAPI GetControl(INT32, CONSTIID, void**) { return E_NOTIMPL; }
    virtual INT32 WINAPI GetControlCount() { return 0; }
    virtual HWND WINAPI GetHandle() { return (HWND)0x4711; }
    virtual BOOL WINAPI HasHandle() { return 1; }
    virtual HRESULT WINAPI SetFocus() { return S_OK; }

    // IAIMPUIBaseEdit / IAIMPUIBaseButtonnedEdit
    virtual HRESULT WINAPI CopyToClipboard() { return S_OK; }
    virtual HRESULT WINAPI CutToClipboard() { return S_OK; }
    virtual HRESULT WINAPI PasteFromClipboard() { return S_OK; }
    virtual HRESULT WINAPI SelectAll() { return S_OK; }
    virtual HRESULT WINAPI SelectNone() { return S_OK; }
    virtual HRESULT WINAPI AddButton(IUnknown*, IAIMPUIEditButton**) { return E_NOTIMPL; }
    virtual HRESULT WINAPI DeleteButton(INT32) { return E_NOTIMPL; }
    virtual HRESULT WINAPI DeleteButton2(IAIMPUIEditButton*) { return E_NOTIMPL; }
    virtual HRESULT WINAPI GetButton(INT32, IAIMPUIEditButton**) { return E_NOTIMPL; }
    virtual INT32 WINAPI GetButtonCount() { return 0; }

    // IAIMPUIBaseComboBox
    virtual HRESULT WINAPI Add(IUnknown* obj, INT32) { items.push_back(StrOf(obj)); return S_OK; }
    virtual HRESULT WINAPI Add2(IAIMPObjectList*) { return E_NOTIMPL; }
    virtual HRESULT WINAPI Clear() { items.clear(); return S_OK; }
    virtual HRESULT WINAPI Delete(INT32) { return E_NOTIMPL; }
    virtual HRESULT WINAPI GetItem(INT32, CONSTIID, void**) { return E_NOTIMPL; }
    virtual INT32 WINAPI GetItemCount() { return (INT32)items.size(); }
    virtual HRESULT WINAPI SetItem(INT32, IUnknown*) { return E_NOTIMPL; }

    // IAIMPUIButton
    virtual HRESULT WINAPI ShowDropDownMenu() { return S_OK; }

    // IAIMPUIPageControl
    virtual HRESULT WINAPI Add(IAIMPString* name, IAIMPUITabSheet** page);
    virtual HRESULT WINAPI Delete2(IAIMPUITabSheet*) { return E_NOTIMPL; }
    virtual HRESULT WINAPI Get(INT32, IAIMPUITabSheet**) { return E_NOTIMPL; }
    virtual INT32 WINAPI GetCount() { return 0; }

    // IAIMPUIForm
    virtual HRESULT WINAPI Close() { return S_OK; }
    virtual HRESULT WINAPI GetFocusedControl(IAIMPUIWinControl**) { return E_NOTIMPL; }
    virtual HRESULT WINAPI Localize() { return S_OK; }
    virtual HRESULT WINAPI Destroy(BOOL) { destroyed = true; return S_OK; }
    virtual INT32 WINAPI ShowModal() { return 0; }
    bool destroyed = false;

private:
    Registry* reg_;
    IUnknown* events_;
    std::atomic<long> ref_{1};
};

// all objects created through the UI service, in creation order (the test keeps one reference each)
struct Registry {
    std::vector<IUnknown*> objects;

    template <class I>
    Ctl<I>* Make(const TStr& kind, const TStr& name, IUnknown* events) {
        Ctl<I>* c = new Ctl<I>(this, kind, name, events);
        c->AddRef();   // registry reference
        objects.push_back(static_cast<I*>(c));
        all.push_back([c]() {
            return Info{c->kind, c->name, &c->ints, &c->strs, &c->items, [c]() { c->FireChanged(); },
                        c->placed, c->alignment, c->anchors, c->bounds};
        });
        return c;
    }

    struct Info {
        TStr kind, name;
        std::map<int, long long>* ints;
        std::map<int, TStr>* strs;
        std::vector<TStr>* items;
        std::function<void()> fire;
        bool placed;
        int alignment;
        RECT anchors, bounds;
    };
    std::vector<std::function<Info()>> all;

    ~Registry() {
        for (IUnknown* o : objects) o->Release();
    }
};

IAIMPString* NewString(const std::basic_string<TChar>& s);   // provided by host_test.cpp

template <class I>
HRESULT WINAPI Ctl<I>::GetValueAsObject(INT32 id, CONSTIID iid, void** v) {
    auto it = strs.find(id);
    if (it == strs.end() || memcmp(&iid, &IID_IAIMPString, sizeof(GUID)) != 0) return E_FAIL;
    *v = NewString(it->second);
    return S_OK;
}

template <class I>
HRESULT WINAPI Ctl<I>::Add(IAIMPString* name, IAIMPUITabSheet** page) {
    if (!page) return E_POINTER;
    Ctl<IAIMPUITabSheet>* sheet = reg_->template Make<IAIMPUITabSheet>(TStr(1, 'S'), StrOf(name), nullptr);
    *page = sheet;   // reference for the caller
    return S_OK;
}

class Service final : public IAIMPServiceUI {
public:
    Registry reg;
    HRESULT __unknwncall QueryInterface(REFIID, LPVOID* ppv) override {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    DWORD __unknwncall AddRef() override { return 2; }
    DWORD __unknwncall Release() override { return 1; }   // lives as long as the test

    HRESULT WINAPI CreateControl(IAIMPUIForm*, IAIMPUIWinControl*, IAIMPString* name, IUnknown* events, CONSTIID iid,
                                 void** obj) override {
        TStr n = StrOf(name);
        auto is = [&](const GUID& g) { return memcmp(&iid, &g, sizeof(GUID)) == 0; };
        if (is(IID_IAIMPUILabel))       { *obj = static_cast<IAIMPUILabel*>(reg.Make<IAIMPUILabel>(K("Label"), n, events)); return S_OK; }
        if (is(IID_IAIMPUICheckBox))    { *obj = static_cast<IAIMPUICheckBox*>(reg.Make<IAIMPUICheckBox>(K("Check"), n, events)); return S_OK; }
        if (is(IID_IAIMPUIEdit))        { *obj = static_cast<IAIMPUIEdit*>(reg.Make<IAIMPUIEdit>(K("Edit"), n, events)); return S_OK; }
        if (is(IID_IAIMPUIComboBox))    { *obj = static_cast<IAIMPUIComboBox*>(reg.Make<IAIMPUIComboBox>(K("Combo"), n, events)); return S_OK; }
        if (is(IID_IAIMPUIButton))      { *obj = static_cast<IAIMPUIButton*>(reg.Make<IAIMPUIButton>(K("Button"), n, events)); return S_OK; }
        if (is(IID_IAIMPUIPageControl)) { *obj = static_cast<IAIMPUIPageControl*>(reg.Make<IAIMPUIPageControl>(K("Tabs"), n, events)); return S_OK; }
        return E_NOTIMPL;
    }
    HRESULT WINAPI CreateForm(HWND, DWORD, IAIMPString* name, IUnknown* events, IAIMPUIForm** form) override {
        Ctl<IAIMPUIForm>* f = reg.Make<IAIMPUIForm>(K("Form"), StrOf(name), events);
        lastForm = f;
        *form = f;
        return S_OK;
    }
    HRESULT WINAPI CreateObject(IAIMPUIForm*, IUnknown*, CONSTIID, void**) override { return E_NOTIMPL; }

    Ctl<IAIMPUIForm>* lastForm = nullptr;

    static TStr K(const char* s) { return TStr(s, s + strlen(s)); }
};

class OptionsService final : public IAIMPServiceOptionsDialog {
public:
    int modified = 0;
    HRESULT __unknwncall QueryInterface(REFIID, LPVOID* ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
    DWORD __unknwncall AddRef() override { return 2; }
    DWORD __unknwncall Release() override { return 1; }
    HRESULT WINAPI FrameModified(IAIMPOptionsDialogFrame*) override { ++modified; return S_OK; }
    HRESULT WINAPI FrameShow(IUnknown*, BOOL) override { return S_OK; }
};

}  // namespace mockui
