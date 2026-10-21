// Unit test of the text templates (src/template.cpp): placeholders, advanced AIMP-style functions, variants.
// Built by CMake as template_test (not by default); run by tests/run_linux_test.sh and tests/run_wine_test.sh.
#include <algorithm>
#include <cstdio>
#include <string>

#include "template.h"
#include "util.h"

#ifdef _WIN32
HINSTANCE g_hModule = nullptr;   // util.cpp (the plugin sets it in DllMain)
#endif

namespace {

int g_failed = 0;

void Eq(const char* what, const std::wstring& got, const std::wstring& want) {
    if (got == want) return;
    ++g_failed;
    std::printf("FAILED %s\n  got:  %s\n  want: %s\n", what, util::ToUtf8(got).c_str(), util::ToUtf8(want).c_str());
}

}  // namespace

int main() {
    tmpl::Vars v = {{L"artist", L"Queen"}, {L"title", L"Bohemian Rhapsody"}, {L"comment", L""}, {L"playcount", L"7"},
                    {L"filesize", L"5.20 MB"}, {L"composer", L"Freddie Mercury"}, {L"bitrate", L"320"}};
    using tmpl::ExpandAdvanced;

    // simple mode: exactly as before
    Eq("simple", tmpl::Expand(L"%artist% - %title% %unknown% 100%", v, false), L"Queen - Bohemian Rhapsody %unknown% 100%");
    Eq("simple keeps functions", tmpl::Expand(L"%IF(%comment%,a,b)", v, false), L"%IF(,a,b)");

    // advanced: the same placeholders give the same text
    Eq("adv plain", ExpandAdvanced(L"%artist% - %title% %unknown% 100% 50% off", v),
       L"Queen - Bohemian Rhapsody %unknown% 100% 50% off");
    Eq("adv aimp style", ExpandAdvanced(L"%Artist - %TITLE", v), L"Queen - Bohemian Rhapsody");
    Eq("adv adjacent", ExpandAdvanced(L"%artist%%title%", v), L"QueenBohemian Rhapsody");
    Eq("adv new fields", ExpandAdvanced(L"%composer% (%bitrate% kbps)", v), L"Freddie Mercury (320 kbps)");

    // %IF(A,B,C): A not empty -> B, else C
    Eq("if empty", ExpandAdvanced(L"%IF(%comment%,%comment%,no comment)", v), L"no comment");
    Eq("if set", ExpandAdvanced(L"%IF(%composer%,by %composer%,-)", v), L"by Freddie Mercury");
    Eq("if case", ExpandAdvanced(L"%if(%Composer,yes,no)", v), L"yes");
    Eq("if trailing %", ExpandAdvanced(L"%IF(%comment%,a,b)% end", v), L"b end");
    Eq("if then placeholder", ExpandAdvanced(L"%IF(x,a,b)%title%", v), L"aBohemian Rhapsody");
    Eq("if missing else", ExpandAdvanced(L"[%IF(%comment%,a)]", v), L"[]");

    // comparisons: numbers by value, texts without case
    Eq("equal num", ExpandAdvanced(L"%IFEqual(%playcount%,7,seven,other)", v), L"seven");
    Eq("equal num 07", ExpandAdvanced(L"%IFEqual(%playcount%,07,seven,other)", v), L"seven");
    Eq("equal text", ExpandAdvanced(L"%IFEqual(%artist%,QUEEN,yes,no)", v), L"yes");
    Eq("greater", ExpandAdvanced(L"%IFGreater(%playcount%,10,many,few)", v), L"few");
    Eq("greater 2 vs 10", ExpandAdvanced(L"%IFGreater(10,2,yes,no)", v), L"yes");   // not "10" < "2"
    Eq("greater equal", ExpandAdvanced(L"%IFGreaterOrEqual(%playcount%,7,yes,no)", v), L"yes");
    Eq("greater empty", ExpandAdvanced(L"%IFGreater(%comment%,1,yes,no)", v), L"no");

    // the example from GitHub issue #4 (AIMP syntax, nested)
    const std::wstring issue =
        L"%If(%Comment,%Comment,%IFEqual(%PlayCount,1,This song has been played once before,"
        L"%IFGreater(%PlayCount,1,This song has been played %PlayCount times,%FileSize)))";
    Eq("issue 7", ExpandAdvanced(issue, v), L"This song has been played 7 times");
    tmpl::Vars v1 = v;
    v1[L"playcount"] = L"1";
    Eq("issue 1", ExpandAdvanced(issue, v1), L"This song has been played once before");
    v1[L"playcount"] = L"";
    Eq("issue none", ExpandAdvanced(issue, v1), L"5.20 MB");
    v1[L"comment"] = L"Live at Wembley";
    Eq("issue comment", ExpandAdvanced(issue, v1), L"Live at Wembley");

    // escapes and broken input never crash and keep the text
    Eq("escape comma", ExpandAdvanced(L"%IF(x,a\\, b,c)", v), L"a, b");
    Eq("escape paren", ExpandAdvanced(L"%IF(x,\\(live\\),c)", v), L"(live)");
    Eq("escape percent", ExpandAdvanced(L"\\%title\\%", v), L"%title%");
    Eq("unclosed", ExpandAdvanced(L"%IF(%comment%,a,b", v), L"b");
    Eq("stray", ExpandAdvanced(L"a) b, c", v), L"a) b, c");
    Eq("unknown func", ExpandAdvanced(L"%Upper(%title%)", v), L"%Upper(Bohemian Rhapsody)");
    std::wstring deep;
    for (int i = 0; i < 200; ++i) deep += L"%IF(x,";
    ExpandAdvanced(deep, v);   // must not overflow the stack

    // variants: in advanced mode only a "||" outside of brackets splits
    Eq("variants simple", std::to_wstring(tmpl::Variants(L"%artist% || %title%", false).size()), L"2");
    Eq("variants none", std::to_wstring(tmpl::Variants(L"%title%", true).size()), L"1");
    const auto parts = tmpl::Variants(L"%IF(%comment%,a || b,c) || %title%", true);
    Eq("variants adv count", std::to_wstring(parts.size()), L"2");
    if (parts.size() == 2) {
        Eq("variants adv 1", parts[0], L"%IF(%comment%,a || b,c)");
        Eq("variants adv 2", parts[1], L"%title%");
    }

    // multi-line field -> one line
    Eq("flatten", tmpl::Flatten(L"%IF(%comment%,%comment%,\r\n   %IFEqual(%playcount%,1,once,\n  more))"),
       L"%IF(%comment%,%comment%,%IFEqual(%playcount%,1,once,more))");
    Eq("flatten words", tmpl::Flatten(L"%bitrate% kbps\n  - %IF(%comment%,a,b)"), L"%bitrate% kbps - %IF(%comment%,a,b)");
    Eq("flatten before bracket", tmpl::Flatten(L"%IF(a,b,c\n)"), L"%IF(a,b,c)");
    Eq("flatten single", tmpl::Flatten(L"%artist% - %title%"), L"%artist% - %title%");

    // multi-line view of a one-line text and back
    const std::wstring pretty = tmpl::Pretty(issue);
    Eq("pretty lines", std::to_wstring(std::count(pretty.begin(), pretty.end(), L'\n')), L"2");
    Eq("pretty round trip", tmpl::Flatten(pretty), issue);
    Eq("pretty plain", tmpl::Pretty(L"%artist% - %title%"), L"%artist% - %title%");
    Eq("pretty escaped", tmpl::Flatten(tmpl::Pretty(L"%IF(a\\,%IF(b,c,d),e)")), L"%IF(a\\,%IF(b,c,d),e)");

    // which placeholders a text uses (periodic refresh for %pos% / %bar% / %percent%)
    Eq("uses simple", tmpl::Uses(L"%bar% %pos%", {L"pos"}, false) ? L"1" : L"0", L"1");
    Eq("uses aimp style only adv", tmpl::Uses(L"%pos x", {L"pos"}, false) ? L"1" : L"0", L"0");
    Eq("uses aimp style", tmpl::Uses(L"%IF(%pos,a,b)", {L"pos"}, true) ? L"1" : L"0", L"1");
    Eq("uses prefix", tmpl::Uses(L"%position%", {L"pos"}, true) ? L"1" : L"0", L"0");

    std::printf(g_failed ? "template test: %d FAILED\n" : "template test: all OK\n", g_failed);
    return g_failed ? 1 : 0;
}
