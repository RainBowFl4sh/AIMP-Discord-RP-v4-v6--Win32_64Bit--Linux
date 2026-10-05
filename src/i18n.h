// Translations. The plugin follows AIMP's interface language automatically (or the language chosen on the
// Advanced tab). English, German, Russian and Ukrainian are built in (langs/*.lng, embedded at build time);
// more languages - or corrections - can be added as Langs/<name>.lng next to the plugin without rebuilding.
#pragma once
#include <string>
#include <vector>

namespace i18n {

struct Language {
    std::wstring code;   // "en", "de", ...
    std::wstring name;   // in its own language, e.g. "Deutsch"
};

void SetAimpHints(const std::vector<std::wstring>& hints);   // what AIMP reports about its language (MUI service)
void SetOverride(const std::wstring& code);                   // Config::language, "" = follow AIMP
std::vector<Language> Available();
std::wstring Active();                                         // code of the language in use
std::wstring AimpLanguage();                                   // name of the detected AIMP language ("" = unknown)
std::wstring T(const char* key);                               // text in the active language (English if missing)

}  // namespace i18n
