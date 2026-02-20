#include "languages.h"
#include <windows.h>
#include <string>
#include <map>

static std::wstring Utf8ToW_local(const std::string &s) {
    if (s.empty()) return {};
    int required = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (required == 0) return {};
    std::wstring out(required, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], required);
    return out;
}

std::map<std::wstring, std::wstring> GetCanonicalDisplayNames() {
    std::map<std::wstring, std::wstring> m;
    // Order here controls dropdown order if desired by caller. Use readable names.
    m[L"en_GB"] = Utf8ToW_local(u8"English");
    m[L"de_DE"] = Utf8ToW_local(u8"Deutsch");
    m[L"de_CH"] = Utf8ToW_local(u8"Deutsch (Schweiz)");
    m[L"fr_FR"] = Utf8ToW_local(u8"Français");
    m[L"fr_CH"] = Utf8ToW_local(u8"Français (Suisse)");
    m[L"it_IT"] = Utf8ToW_local(u8"Italiano");
    m[L"it_CH"] = Utf8ToW_local(u8"Italiano (Svizzera)");
    m[L"nl_NL"] = Utf8ToW_local(u8"Nederlands");
    m[L"nl_BE"] = Utf8ToW_local(u8"Nederlands (België)");
    m[L"da_DK"] = Utf8ToW_local(u8"Dansk");
    m[L"sv_SE"] = Utf8ToW_local(u8"Svenska");
    m[L"no_NB"] = Utf8ToW_local(u8"Norsk (Bokmål)");
    m[L"no_NN"] = Utf8ToW_local(u8"Norsk (Nynorsk)");
    m[L"is_IS"] = Utf8ToW_local(u8"Íslenska");
    m[L"pl_PL"] = Utf8ToW_local(u8"Polski");
    m[L"pt_PT"] = Utf8ToW_local(u8"Português");
    m[L"es_ES"] = Utf8ToW_local(u8"Español");
    m[L"ro_RO"] = Utf8ToW_local(u8"Română");
    m[L"el_GR"] = Utf8ToW_local(u8"Ελληνικά");
    m[L"uk_UA"] = Utf8ToW_local(u8"Українська");
    m[L"rm_CH"] = Utf8ToW_local(u8"Rumantsch");
    // If callers have other locale files, they'll get a fallback name.
    return m;
}
