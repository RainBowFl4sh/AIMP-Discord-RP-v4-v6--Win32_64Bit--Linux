// Small helpers around the AIMP SDK that work the same on Windows and Linux:
// IAIMPString <-> std::wstring (UTF-16 on Windows, UTF-8 on Linux), a minimal COM smart pointer, IID compare.
#pragma once
#ifdef _WIN32
#include <windows.h>
#endif

#include "apiCore.h"
#include "apiObjects.h"

#include <cstring>
#include <string>

#include "util.h"

#ifndef _WIN32
#ifndef FAILED
#define FAILED(hr)    Failed(hr)
#define SUCCEEDED(hr) Succeeded(hr)
#endif
#endif

// TChar literal: L"..." on Windows, "..." (UTF-8) on Linux
#ifdef _WIN32
#define AIMP_TEXT(s) L##s
#else
#define AIMP_TEXT(s) s
#endif

namespace aimp {

inline bool SameIID(REFIID a, REFIID b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }

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
    T* detach() { T* p = p_; p_ = nullptr; return p; }
private:
    T* p_ = nullptr;
};

inline std::wstring FromAimp(const TChar* d, int n) {
#ifdef _WIN32
    return std::wstring(d, (size_t)n);
#else
    return util::FromUtf8(std::string(d, (size_t)n));
#endif
}

inline std::wstring StringOf(IAIMPString* s) {
    if (!s) return std::wstring();
    PChar d = s->GetData();
    int n = s->GetLength();
    return (d && n > 0) ? FromAimp(d, n) : std::wstring();
}

// New IAIMPString (reference owned by the caller) or nullptr.
inline IAIMPString* MakeString(IAIMPCore* core, const std::wstring& text) {
    IAIMPString* s = nullptr;
    if (!core || FAILED(core->CreateObject(IID_IAIMPString, reinterpret_cast<void**>(&s))) || !s) return nullptr;
#ifdef _WIN32
    s->SetData(const_cast<PChar>(text.c_str()), (INT32)text.size());
#else
    std::string u = util::ToUtf8(text);
    s->SetData(const_cast<PChar>(u.c_str()), (INT32)u.size());
#endif
    return s;
}

// AIMP's version as major * 100 + minor (e.g. 540 for 5.40), 0 when AIMP does not say
inline int Version(IAIMPCore* core) {
    IAIMPServiceVersionInfo* info = nullptr;
    if (!core || FAILED(core->QueryInterface(IID_IAIMPServiceVersionInfo, reinterpret_cast<void**>(&info))) || !info)
        return 0;
    const int v = info->GetVersionID();   // e.g. 4700 for 4.70, 5400 for 5.40
    info->Release();
    return v >= 1000 ? v / 10 : (v > 0 ? v : 0);
}

inline std::wstring PropString(IAIMPPropertyList* pl, int id) {
    ComPtr<IAIMPString> s;
    if (pl && SUCCEEDED(pl->GetValueAsObject(id, IID_IAIMPString, s.putVoid())) && s) return StringOf(s.get());
    return std::wstring();
}

inline void SetPropString(IAIMPCore* core, IAIMPPropertyList* pl, int id, const std::wstring& text) {
    if (!pl) return;
    IAIMPString* s = MakeString(core, text);
    if (!s) return;
    pl->SetValueAsObject(id, s);
    s->Release();
}

}  // namespace aimp
