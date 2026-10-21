// Text templates of the Display tab: %name% placeholders and, as an option ("advanced placeholders"), AIMP-style
// conditions %IF(A,B,C), %IFEqual(A,B,C,D), %IFGreater(A,B,C,D), %IFGreaterOrEqual(A,B,C,D).
// No AIMP types: also used by the tests without AIMP.
#pragma once
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace tmpl {

using Vars = std::map<std::wstring, std::wstring>;   // lower-case name -> value

// %name% -> value. Unknown placeholders stay untouched. With urlEncode every value is percent-encoded.
std::wstring Expand(const std::wstring& t, const Vars& vars, bool urlEncode);

// Advanced: like Expand, plus the AIMP functions above (nested, names without case) and fields written the AIMP way
// without the closing % ("%PlayCount times"). Arguments are split at "," - "\," "\(" "\)" "\%" "\\" are literal.
std::wstring ExpandAdvanced(const std::wstring& t, const Vars& vars);

// "%artist% || %title%" -> the variants. Advanced: only a "||" outside of function brackets splits.
std::vector<std::wstring> Variants(const std::wstring& t, bool advanced);

// Text of a multi-line field -> one line: every line break and the indentation around it is removed.
std::wstring Flatten(const std::wstring& multiLine);

// One line -> the same text spread over several lines for a multi-line field: every function inside a function
// starts a new line, indented by its depth. Flatten(Pretty(t)) == t.
std::wstring Pretty(const std::wstring& oneLine);

// The text uses one of these placeholders (also the AIMP way without the closing %)
bool Uses(const std::wstring& t, std::initializer_list<const wchar_t*> names, bool advanced);

}  // namespace tmpl
