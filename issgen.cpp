/*
 * issgen.cpp — .iss script generator for SetupCraft.
 *
 * Reads inno/template.iss, substitutes every {#Token} placeholder with a
 * per-project value, replaces the "; <<LANGUAGES>>" marker with the correct
 * Inno [Languages] entries, and writes the result to outPath.
 *
 * The generated file is a plain ISS file with no preprocessor directives;
 * all {#...} tokens are expanded by this code before ISCC sees the file.
 * Inno runtime constants ({app}, {pf}, {group}, …) are NOT touched — they
 * remain in the file for ISCC to resolve at compile / install time.
 */

#include "issgen.h"
#include <algorithm>
#include <map>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Convert a wide string to a lowercase copy.
static std::wstring ToLower(const std::wstring& s)
{
    std::wstring out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c){ return (wchar_t)towlower(c); });
    return out;
}

// Escape every '{' and '}' in s to '{{' and '}}' (Inno directive-value escaping).
// Used for the AppId directive, which must contain literal braces.
static std::wstring EscapeBraces(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size() + 4);
    for (wchar_t c : s) {
        if      (c == L'{') out += L"{{";
        else if (c == L'}') out += L"}}";
        else                 out += c;
    }
    return out;
}

// Replace all occurrences of 'from' with 'to' in 'str' (in-place).
static void ReplaceAll(std::wstring& str,
                       const std::wstring& from,
                       const std::wstring& to)
{
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::wstring::npos) {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// Build the Inno [Languages] section body (no header line) from the given
// language list.  'innoDir' is the directory that contains local .isl files.
// Each line:
//   local=false  →  Name: "english"; MessagesFile: "compiler:Default.isl"
//   local=true   →  Name: "swedish"; MessagesFile: "Swedish.isl"
// The Name: identifier is the ISL base name lowercased.
// Exception: ISL base name "Default" maps to the identifier "english".
static std::wstring BuildLanguagesSection(
    const std::vector<InnoLangEntry>& langs)
{
    std::wstring out;
    for (const auto& lang : langs) {
        std::wstring name = (lang.isl == L"Default") ? L"english" : ToLower(lang.isl);
        out += L"Name: \"";
        out += name;
        out += L"\"; MessagesFile: \"";
        if (lang.local) {
            // Community .isl — relative path (same directory as the .iss file)
            out += lang.isl;
            out += L".isl";
        } else {
            out += L"compiler:";
            out += lang.isl;
            out += L".isl";
        }
        out += L"\"\r\n";
    }
    return out;
}

// Map compressionType index to the Inno Compression= string.
static const wchar_t* CompressionStr(int type)
{
    switch (type) {
        case 0:  return L"none";
        case 1:  return L"zip";
        case 2:  return L"lzma";
        default: return L"lzma2";
    }
}

// Map uacLevel index to the Inno PrivilegesRequired= string.
static const wchar_t* PrivilegesStr(int level)
{
    switch (level) {
        case 1:  return L"lowest";   // asInvoker
        default: return L"admin";    // requireAdministrator (also used for highestAvailable)
    }
}

// Map minOsVersion index to the Inno MinVersion= string.
// Returns L"" for "no minimum" (index 0).
static const wchar_t* MinVersionStr(int ver)
{
    switch (ver) {
        case 1:  return L"6.1";       // Windows 7
        case 2:  return L"6.2";       // Windows 8
        case 3:  return L"6.3";       // Windows 8.1
        case 4:  return L"10.0";      // Windows 10
        case 5:  return L"10.0.22000";// Windows 11
        default: return L"";
    }
}

// Strip a trailing backslash from a path (if present).
static std::wstring StripTrailingSep(const std::wstring& p)
{
    if (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))
        return p.substr(0, p.size() - 1);
    return p;
}

// ── ISS_FindInnoDir ───────────────────────────────────────────────────────────

std::wstring ISS_FindInnoDir()
{
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
        return L"";

    // Strip the filename to get the exe directory.
    std::wstring dir = exePath;
    auto slash = dir.rfind(L'\\');
    if (slash != std::wstring::npos)
        dir = dir.substr(0, slash);

    // Try exe-adjacent inno\ first (deployed layout: SetupCraft.exe + inno\)
    std::wstring candidate = dir + L"\\inno";
    if (GetFileAttributesW((candidate + L"\\template.iss").c_str()) != INVALID_FILE_ATTRIBUTES)
        return candidate;

    // Try one level up (development layout: build\SetupCraft.exe, ..\inno\)
    candidate = dir + L"\\..\\inno";
    wchar_t resolved[MAX_PATH] = {};
    if (GetFullPathNameW(candidate.c_str(), MAX_PATH, resolved, NULL) &&
        GetFileAttributesW((std::wstring(resolved) + L"\\template.iss").c_str()) != INVALID_FILE_ATTRIBUTES)
        return resolved;

    return L"";
}

// ── ISS_GenerateIss ───────────────────────────────────────────────────────────

std::wstring ISS_GenerateIss(
    const std::wstring&              templatePath,
    const std::wstring&              outPath,
    const ProjectRow&                proj,
    const SBuildConfig&              cfg,
    const std::vector<InnoLangEntry>& langs)
{
    // ── Read template ─────────────────────────────────────────────────────────
    // Use Win32 to read the file so wide paths work on MinGW.
    HANDLE hFile = CreateFileW(templatePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return L"Cannot open template: " + templatePath;
    DWORD fileSize = GetFileSize(hFile, nullptr);
    std::string rawBytes(fileSize, '\0');
    DWORD bytesRead = 0;
    ReadFile(hFile, &rawBytes[0], fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);
    // Detect and strip UTF-8 BOM if present.
    const char* dataPtr = rawBytes.data();
    DWORD dataLen = bytesRead;
    if (dataLen >= 3 &&
        (unsigned char)dataPtr[0] == 0xEF &&
        (unsigned char)dataPtr[1] == 0xBB &&
        (unsigned char)dataPtr[2] == 0xBF) {
        dataPtr += 3; dataLen -= 3;
    }
    // Convert UTF-8 → wchar_t.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, dataPtr, (int)dataLen, nullptr, 0);
    std::wstring tmpl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dataPtr, (int)dataLen, &tmpl[0], wlen);

    // ── Build token table  ────────────────────────────────────────────────────
    // Effective output dir: use configured folder, or project directory as fallback.
    std::wstring outDir = StripTrailingSep(
        cfg.outputFolder.empty() ? proj.directory : cfg.outputFolder);
    if (outDir.empty()) {
        // Last resort: same dir as the generated .iss
        std::wstring issDir = outPath;
        auto p = issDir.rfind(L'\\');
        if (p != std::wstring::npos) issDir = issDir.substr(0, p);
        outDir = issDir;
    }

    // Effective output base filename.
    std::wstring outBase = cfg.outputFilename;
    if (outBase.empty())
        outBase = proj.name.empty() ? L"Setup" : proj.name + L"_Setup";

    // AppId: stored as "{GUID}", needs Inno escaping → "{{GUID}}"
    std::wstring appId = EscapeBraces(proj.app_id);

    // SourceDir for the [Files] wildcard.
    std::wstring sourceDir = StripTrailingSep(proj.directory);

    // ExeName defaults to AppName.exe (the project doesn't store it separately yet).
    std::wstring exeName = proj.name.empty() ? L"Setup.exe" : proj.name + L".exe";

    // Publisher: project field, fall back to app_publisher stored separately.
    std::wstring publisher = proj.app_publisher;

    std::map<std::wstring, std::wstring> tokens = {
        { L"AppName",           proj.name                              },
        { L"AppVersion",        proj.version                           },
        { L"Publisher",         publisher                              },
        { L"PublisherURL",      cfg.publisherUrl                       },
        { L"SupportURL",        cfg.supportUrl                         },
        { L"AppId",             appId                                  },
        { L"DefaultDirBase",    SETT_GetInstallBasePath()              },
        { L"OutputDir",         outDir                                 },
        { L"OutputBase",        outBase                                },
        { L"Compression",       CompressionStr(cfg.compressionType)    },
        { L"SolidCompression",  cfg.solidCompression ? L"yes" : L"no" },
        { L"PrivilegesRequired",PrivilegesStr(cfg.uacLevel)            },
        { L"Uninstallable",     cfg.allowUninstall ? L"yes" : L"no"   },
        { L"CloseApplications", cfg.closeApps      ? L"yes" : L"no"  },
        { L"MinVersion",        MinVersionStr(cfg.minOsVersion)        },
        { L"ExeName",           exeName                                },
        { L"SourceDir",         sourceDir                              },
    };

    // ── Substitute {#Token} placeholders ─────────────────────────────────────
    for (const auto& kv : tokens)
        ReplaceAll(tmpl, L"{#" + kv.first + L"}", kv.second);

    // ── Replace the "; <<LANGUAGES>>" marker with actual language entries ─────
    std::wstring langBlock = BuildLanguagesSection(langs);
    ReplaceAll(tmpl, L"; <<LANGUAGES>>", langBlock);

    // ── Write output as UTF-8 with BOM (ISCC accepts UTF-8 BOM) ─────────────
    int needed = WideCharToMultiByte(CP_UTF8, 0,
                                     tmpl.c_str(), (int)tmpl.size(),
                                     nullptr, 0, nullptr, nullptr);
    std::string utf8;
    if (needed > 0) {
        utf8.resize(needed);
        WideCharToMultiByte(CP_UTF8, 0,
                            tmpl.c_str(), (int)tmpl.size(),
                            &utf8[0], needed, nullptr, nullptr);
    }

    HANDLE hOut = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE)
        return L"Cannot write generated script: " + outPath;
    // UTF-8 BOM
    const char bom[] = { '\xEF', '\xBB', '\xBF' };
    DWORD written = 0;
    WriteFile(hOut, bom, 3, &written, nullptr);
    if (!utf8.empty())
        WriteFile(hOut, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(hOut);

    return L""; // success
}
