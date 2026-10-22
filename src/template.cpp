#include "template.h"

#include <cwchar>
#include <cwctype>

#include "util.h"

namespace tmpl {

std::wstring Expand(const std::wstring& t, const Vars& vars, bool urlEncode) {
    std::wstring out;
    for (size_t i = 0; i < t.size();) {
        if (t[i] == L'%') {
            size_t j = t.find(L'%', i + 1);
            if (j != std::wstring::npos) {
                auto it = vars.find(util::Lower(t.substr(i + 1, j - i - 1)));
                if (it != vars.end()) {
                    out += urlEncode ? util::FromUtf8(util::UrlEncode(util::ToUtf8(it->second))) : it->second;
                    i = j + 1;
                    continue;
                }
            }
        }
        out += t[i++];
    }
    return out;
}

namespace {

bool IdentChar(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_'; }
bool Letter(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool Escapable(wchar_t c) { return c == L',' || c == L'(' || c == L')' || c == L'%' || c == L'\\' || c == L'|'; }

#ifdef _WIN32
const wchar_t* const kNewLine = L"\r\n";   // line break of AIMP's multi-line fields
#else
const wchar_t* const kNewLine = L"\n";
#endif

enum Func { kNone, kIf, kEqual, kGreater, kGreaterOrEqual };

Func FuncOf(const std::wstring& lowerName) {
    if (lowerName == L"if")                return kIf;
    if (lowerName == L"ifequal")           return kEqual;
    if (lowerName == L"ifgreater")         return kGreater;
    if (lowerName == L"ifgreaterorequal")  return kGreaterOrEqual;
    return kNone;
}

// A number, also with the unit of %samplerate% / %bitdepth% ("44.1kHz" = 44100, "16bit" = 16): comparisons with the
// bare numbers of older texts keep working
bool Number(const std::wstring& s, double& v) {
    if (s.empty()) return false;
    std::wstring n = s;
    for (wchar_t& c : n) if (c == L',') c = L'.';   // "1,5" as well as "1.5"
    wchar_t* end = nullptr;
    v = std::wcstod(n.c_str(), &end);
    if (!end || end == n.c_str()) return false;
    while (*end == L' ') ++end;
    const std::wstring unit = util::Lower(end);
    if (unit == L"khz") v *= 1000;
    else if (!unit.empty() && unit != L"hz" && unit != L"bit" && unit != L"bits") return false;
    return true;
}

// < 0, 0, > 0: numbers by value, other texts alphabetically without case
int Compare(const std::wstring& a0, const std::wstring& b0) {
    const std::wstring a = util::Trim(a0), b = util::Trim(b0);
    double x, y;
    if (Number(a, x) && Number(b, y)) return x < y ? -1 : (x > y ? 1 : 0);
    const std::wstring la = util::Lower(a), lb = util::Lower(b);
    return la < lb ? -1 : (la > lb ? 1 : 0);
}

class Parser {
public:
    Parser(const std::wstring& t, const Vars& v) : t_(t), v_(v) {}

    // text up to the end, or (inside a function) up to the next "," / ")" of this level
    std::wstring Sequence(size_t& i, bool inArgs, int depth) {
        std::wstring out;
        while (i < t_.size()) {
            const wchar_t c = t_[i];
            if (c == L'\\' && i + 1 < t_.size() && Escapable(t_[i + 1])) {
                out += t_[i + 1];
                i += 2;
            } else if (inArgs && (c == L',' || c == L')')) {
                return out;
            } else if (c == L'%') {
                out += Percent(i, depth);
            } else {
                out += c;
                ++i;
            }
        }
        return out;
    }

private:
    std::wstring Percent(size_t& i, int depth) {
        size_t j = i + 1;
        while (j < t_.size() && IdentChar(t_[j])) ++j;
        const std::wstring name = util::Lower(t_.substr(i + 1, j - i - 1));
        if (name.empty()) {
            ++i;
            return L"%";
        }
        const Func f = FuncOf(name);
        if (f != kNone && j < t_.size() && t_[j] == L'(' && depth < 32) {
            i = j + 1;
            std::vector<std::wstring> args;
            for (;;) {
                args.push_back(Sequence(i, true, depth + 1));
                if (i < t_.size() && t_[i] == L',') { ++i; continue; }
                if (i < t_.size() && t_[i] == L')') ++i;
                break;
            }
            // "%IF(...)%" is accepted too - but "%IF(...)%title%" keeps the "%" of %title%
            if (i < t_.size() && t_[i] == L'%' && (i + 1 >= t_.size() || !Letter(t_[i + 1]))) ++i;
            args.resize(4);
            switch (f) {
                case kIf:             return !util::Trim(args[0]).empty() ? args[1] : args[2];
                case kEqual:          return Compare(args[0], args[1]) == 0 ? args[2] : args[3];
                case kGreater:        return Compare(args[0], args[1]) > 0 ? args[2] : args[3];
                case kGreaterOrEqual: return Compare(args[0], args[1]) >= 0 ? args[2] : args[3];
                default:              return std::wstring();
            }
        }
        auto it = v_.find(name);
        if (it == v_.end()) {   // unknown: stays as it is
            ++i;
            return L"%";
        }
        i = j;
        if (i < t_.size() && t_[i] == L'%') ++i;   // %name% as well as %name
        return it->second;
    }

    const std::wstring& t_;
    const Vars& v_;
};

}  // namespace

std::wstring ExpandAdvanced(const std::wstring& t, const Vars& vars) {
    Parser p(t, vars);
    size_t i = 0;
    std::wstring out;
    while (i < t.size()) {   // a stray ")" or "," at the top level is plain text
        out += p.Sequence(i, false, 0);
        if (i < t.size()) out += t[i++];
    }
    return out;
}

std::vector<std::wstring> Variants(const std::wstring& t, bool advanced) {
    std::vector<std::wstring> parts;
    int depth = 0;
    size_t from = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        const wchar_t c = t[i];
        if (advanced && c == L'\\' && i + 1 < t.size()) { ++i; continue; }
        if (advanced && c == L'(') ++depth;
        else if (advanced && c == L')' && depth > 0) --depth;
        else if (c == L'|' && depth == 0 && i + 1 < t.size() && t[i + 1] == L'|') {
            parts.push_back(util::Trim(t.substr(from, i - from)));
            from = i + 2;
            ++i;
        }
    }
    if (parts.empty()) return {t};
    parts.push_back(util::Trim(t.substr(from)));
    return parts;
}

std::wstring Flatten(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'\r' && s[i] != L'\n') {
            out += s[i];
            continue;
        }
        while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
        while (i + 1 < s.size() && (s[i + 1] == L'\r' || s[i + 1] == L'\n' || s[i + 1] == L' ' || s[i + 1] == L'\t')) ++i;
        // between words the line break was a space; after "," / "(" and before "," / ")" it only served the overview
        const wchar_t next = i + 1 < s.size() ? s[i + 1] : 0;
        if (!out.empty() && next && out.back() != L',' && out.back() != L'(' && next != L',' && next != L')') out += L' ';
    }
    return out;
}

std::wstring Pretty(const std::wstring& t) {
    std::wstring out;
    int depth = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        const wchar_t c = t[i];
        if (c == L'\\' && i + 1 < t.size()) {   // escaped character: as it is
            out += c;
            out += t[++i];
            continue;
        }
        if (c == L'%' && depth > 0 && !out.empty() && (out.back() == L',' || out.back() == L'(')) {
            size_t j = i + 1;
            while (j < t.size() && IdentChar(t[j])) ++j;
            if (j < t.size() && t[j] == L'(' && FuncOf(util::Lower(t.substr(i + 1, j - i - 1))) != kNone)
                out += kNewLine + std::wstring((size_t)depth * 2, L' ');
        }
        if (c == L'(') ++depth;
        else if (c == L')' && depth > 0) --depth;
        out += c;
    }
    return out;
}

bool Uses(const std::wstring& t, std::initializer_list<const wchar_t*> names, bool advanced) {
    const std::wstring l = util::Lower(t);
    for (const wchar_t* n : names) {
        const std::wstring p = std::wstring(L"%") + n;
        for (size_t at = l.find(p); at != std::wstring::npos; at = l.find(p, at + 1)) {
            const size_t end = at + p.size();
            if (end < l.size() && l[end] == L'%') return true;
            if (advanced && (end >= l.size() || !IdentChar(l[end]))) return true;
        }
    }
    return false;
}

namespace {

bool Digits(const std::wstring& s) {
    if (s.empty() || s.size() > 9) return false;
    for (wchar_t c : s) if (c < L'0' || c > L'9') return false;
    return true;
}

}  // namespace

std::wstring SampleRateText(const std::wstring& hz) {
    if (!Digits(hz)) return hz;
    const long v = std::wcstol(hz.c_str(), nullptr, 10);
    std::wstring out = std::to_wstring(v / 1000);
    std::wstring frac = std::to_wstring(1000 + v % 1000).substr(1);   // three digits, without trailing zeros
    while (!frac.empty() && frac.back() == L'0') frac.pop_back();
    if (!frac.empty()) out += L"." + frac;
    return out + L"kHz";
}

std::wstring BitDepthText(const std::wstring& bits) { return Digits(bits) ? bits + L"bit" : bits; }

std::wstring DropOldUnits(const std::wstring& t) {
    static const struct { const wchar_t* name; std::initializer_list<const wchar_t*> units; } kFields[] = {
        {L"%samplerate", {L"khz", L"hz"}}, {L"%bitdepth", {L"bits", L"bit"}}};
    std::wstring out = t;
    for (const auto& f : kFields) {
        const std::wstring name = f.name;
        for (size_t at = util::Lower(out).find(name); at != std::wstring::npos;
             at = util::Lower(out).find(name, at + 1)) {
            size_t i = at + name.size();
            if (i < out.size() && IdentChar(out[i])) continue;   // another name (%samplerates...)
            if (i < out.size() && out[i] == L'%') ++i;
            size_t u = i;
            while (u < out.size() && (out[u] == L' ' || out[u] == L'-')) ++u;
            const std::wstring rest = util::Lower(out.substr(u, 4));
            for (const wchar_t* unit : f.units) {
                const size_t n = std::wcslen(unit);
                if (rest.compare(0, n, unit) != 0 || (u + n < out.size() && Letter(out[u + n]))) continue;
                out.erase(i, u + n - i);
                break;
            }
        }
    }
    return out;
}

}  // namespace tmpl
