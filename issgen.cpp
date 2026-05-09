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

// Build the Inno [Types] section body (no header line) from the given type list.
// Each line:  Name: "full"; Description: "Full installation"[; Flags: iscustom]
// Returns an empty string if the list is empty (section will be omitted).
static std::wstring BuildTypesSection(const std::vector<InstallTypeRow>& types)
{
    std::wstring out;
    for (const auto& t : types) {
        if (t.name.empty()) continue;
        out += L"Name: \"" + t.name + L"\"; Description: \"" + t.description + L"\"";
        if (t.is_custom)
            out += L"; Flags: iscustom";
        out += L"\r\n";
    }
    return out;
}

// Build the Inno [Components] section body (no header line) from the given component list.
// Components with a group_name get Name: "group\comp" and the group header is emitted once.
// is_required maps to Flags: fixed (always installed regardless of type selection).
// Returns an empty string if the list is empty.
static std::wstring BuildComponentsSection(const std::vector<ComponentRow>& comps)
{
    // Helper: normalise an identifier (lowercase, spaces→underscores).
    auto ident = [](const std::wstring& s) {
        std::wstring out = s;
        for (auto& ch : out) ch = (ch == L' ') ? L'_' : (wchar_t)towlower(ch);
        return out;
    };

    std::wstring out;

    // Emit one group-header row per unique non-empty group_name (first occurrence wins
    // for description — group rows carry no Types/Flags of their own).
    std::vector<std::wstring> seenGroups;
    for (const auto& c : comps) {
        if (c.group_name.empty()) continue;
        std::wstring gid = ident(c.group_name);
        bool seen = false;
        for (const auto& s : seenGroups) if (s == gid) { seen = true; break; }
        if (seen) continue;
        seenGroups.push_back(gid);
        // The group description is the group_name itself (human-readable).
        out += L"Name: \"" + gid + L"\"; Description: \"" + c.group_name + L"\"\r\n";
    }

    for (const auto& c : comps) {
        if (c.display_name.empty()) continue;
        if (c.is_required) continue;  // is_required = always installed silently (not listed in [Components])
        std::wstring compId = ident(c.display_name);
        if (!c.group_name.empty())
            compId = ident(c.group_name) + L"\\" + compId;
        out += L"Name: \"" + compId + L"\"; Description: \"" + c.description + L"\"";
        if (!c.install_types.empty())
            out += L"; Types: " + c.install_types;
        if (c.is_fixed)
            out += L"; Flags: fixed";  // is_fixed = visible in wizard but greyed-out
        out += L"\r\n";
    }
    return out;
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

// Map privOverridesAllowed index to the Inno PrivilegesRequiredOverridesAllowed= string.
// Controls whether the end user can switch between per-user / all-users at runtime.
// "dialog" enables the For Me/All Users wizard page.
static const wchar_t* PrivOverridesStr(int val)
{
    switch (val) {
        case 1:  return L"commandline";
        case 2:  return L"dialog";
        default: return L"none";
    }
}

// Map wizardStyle index to the Inno WizardStyle= string.
static const wchar_t* WizardStyleStr(int val)
{
    return (val == 1) ? L"classic" : L"modern";
}

// Map dirExistsWarning index to the Inno DirExistsWarning= string.
static const wchar_t* DirExistsWarningStr(int val)
{
    switch (val) {
        case 1:  return L"yes";
        case 2:  return L"no";
        default: return L"auto";
    }
}

// Map langDetectionMethod index to the Inno LanguageDetectionMethod= string.
static const wchar_t* LangDetectionMethodStr(int val)
{
    switch (val) {
        case 1:  return L"locale";
        case 2:  return L"none";
        default: return L"uilanguage";
    }
}

// Map showLanguageDialog index to the Inno ShowLanguageDialog= string.
static const wchar_t* ShowLanguageDialogStr(int val)
{
    switch (val) {
        case 1:  return L"yes";
        case 2:  return L"no";
        default: return L"auto";
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

// Escape single quotes in a Pascal string literal by doubling them.
static std::wstring EscapePascalString(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s)
        out += (c == L'\'') ? L"''" : std::wstring(1, c);
    return out;
}

// Build the SignTool= directive line for Inno Setup, or return L"" if disabled.
// Inno uses: SignTool=<name> <command-template>  where $f = output .exe path.
// The directive is inserted in [Setup] and ISCC calls the command after linking.
static std::wstring BuildSignToolLine(const SBuildConfig& cfg)
{
    if (!cfg.signEnabled) return L"";

    // Signtool path — default to bare "signtool.exe" so it is found via PATH.
    std::wstring tool = cfg.signtoolPath.empty() ? L"signtool.exe"
                                                 : L"\"" + cfg.signtoolPath + L"\"";

    // Build the signtool argument list.
    std::wstring args = L"sign";

    // Certificate: prefer thumbprint (no password), fall back to PFX.
    if (!cfg.signThumbprint.empty()) {
        args += L" /sha1 " + cfg.signThumbprint;
    } else if (!cfg.signPfxPath.empty()) {
        args += L" /f \"" + cfg.signPfxPath + L"\"";
        if (!cfg.signPfxPassword.empty())
            args += L" /p \"" + cfg.signPfxPassword + L"\"";
    }

    // Digest algorithm for the file signature.
    const wchar_t* fdAlgo = (cfg.signTimestampAlgo == 0) ? L"sha1" : L"sha256";
    args += std::wstring(L" /fd ") + fdAlgo;

    // Timestamp.
    if (!cfg.signTimestampUrl.empty()) {
        if (cfg.signTimestampAlgo == 0) {
            // SHA-1 timestamp (old /t syntax)
            args += L" /t " + cfg.signTimestampUrl;
        } else {
            // RFC-3161 timestamp
            args += L" /tr " + cfg.signTimestampUrl;
            args += L" /td sha256";
        }
    }

    // Optional description.
    if (!cfg.signDescription.empty())
        args += L" /d \"" + cfg.signDescription + L"\"";

    // $f is Inno's placeholder for the output installer path.
    args += L" $f";

    // Inno requires a tool name (arbitrary identifier) before the command.
    return L"SignTool=sc " + tool + L" " + args;
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
    const std::wstring&                  templatePath,
    const std::wstring&                  outPath,
    const ProjectRow&                    proj,
    const SBuildConfig&                  cfg,
    const std::vector<InnoLangEntry>&    langs,
    const std::vector<FileAssocRow>&     assocs,
    const std::vector<InstallTypeRow>&   types,
    const std::vector<ComponentRow>&     comps)
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

    // SetupMutex: derived from raw AppId, stripped of braces → "Global\GUID_Setup"
    // Prevents two copies of the installer running simultaneously (always on).
    std::wstring rawId = proj.app_id;
    if (!rawId.empty() && rawId.front() == L'{') rawId = rawId.substr(1);
    if (!rawId.empty() && rawId.back()  == L'}') rawId = rawId.substr(0, rawId.size() - 1);
    std::wstring setupMutex = rawId.empty() ? L"" : (L"Global\\" + rawId + L"_Setup");

    // SourceDir for the [Files] wildcard.
    std::wstring sourceDir = StripTrailingSep(proj.directory);

    // ExeName defaults to AppName.exe (the project doesn't store it separately yet).
    std::wstring exeName = proj.name.empty() ? L"Setup.exe" : proj.name + L".exe";

    // Publisher: project field, fall back to app_publisher stored separately.
    std::wstring publisher = proj.app_publisher;

    // Copyright string: "© YYYY Publisher" (or just "© YYYY" if no publisher).
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    std::wstring year = std::to_wstring(st.wYear);
    std::wstring copyright = publisher.empty()
        ? (L"\u00A9 " + year)
        : (L"\u00A9 " + year + L" " + publisher);

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
        { L"PrivilegesRequired",             PrivilegesStr(cfg.uacLevel)                        },
        { L"PrivilegesRequiredOverridesAllowed", PrivOverridesStr(cfg.privOverridesAllowed)     },
        { L"WizardStyle",      WizardStyleStr(cfg.wizardStyle)                                  },
        { L"Uninstallable",     cfg.allowUninstall ? L"yes" : L"no"   },
        { L"CloseApplications", cfg.closeApps      ? L"yes" : L"no"  },
        { L"ChangesEnvironment",  cfg.changesEnvironment  ? L"yes" : L"no" },
        { L"ChangesAssociations", cfg.changesAssociations ? L"yes" : L"no" },
        { L"SetupLogging",        cfg.setupLogging        ? L"yes" : L"no" },
        { L"DisableDirPage",           cfg.disableDirPage          ? L"yes" : L"no"  },
        { L"DisableProgramGroupPage",   cfg.disableProgramGroupPage ? L"yes" : L"no"  },
        { L"UsePreviousAppDir",         cfg.usePreviousAppDir       ? L"yes" : L"no"  },
        { L"UsePreviousGroup",          cfg.usePreviousGroup        ? L"yes" : L"no"  },
        { L"DirExistsWarning",          DirExistsWarningStr(cfg.dirExistsWarning)       },
        { L"MinVersion",        MinVersionStr(cfg.minOsVersion)        },
        { L"ExeName",           exeName                                },
        { L"SourceDir",         sourceDir                              },
        // Installer .exe file-version resource (right-click → Properties → Details)
        { L"VersionInfoVersion",       proj.version                                               },
        { L"VersionInfoTextVersion",   proj.version                                               },
        { L"VersionInfoDescription",   proj.name.empty() ? L"Setup" : proj.name + L" Setup"      },
        { L"VersionInfoProductName",   proj.name                                                  },
        { L"VersionInfoProductVersion",proj.version                                               },
        { L"VersionInfoCompany",       publisher                                                  },
        { L"VersionInfoCopyright",     copyright                                                  },
        { L"AppCopyright",             copyright                                                  },
        { L"SignToolLine",             BuildSignToolLine(cfg)                                     },
        { L"SetupMutex",               setupMutex                                                 },
        { L"UninstallDisplayName",     cfg.uninstallDisplayName.empty() ? proj.name : cfg.uninstallDisplayName },
        { L"UninstallFilesDir",        cfg.uninstallFilesDir.empty() ? L"{app}" : cfg.uninstallFilesDir },
        { L"LanguageDetectionMethod",  LangDetectionMethodStr(cfg.langDetectionMethod) },
        { L"ShowLanguageDialog",       ShowLanguageDialogStr(cfg.showLanguageDialog)    },
    };

    // ── Substitute {#Token} placeholders ─────────────────────────────────────
    for (const auto& kv : tokens)
        ReplaceAll(tmpl, L"{#" + kv.first + L"}", kv.second);

    // ── Replace the "; <<LANGUAGES>>" marker with actual language entries ─────
    std::wstring langBlock = BuildLanguagesSection(langs);
    ReplaceAll(tmpl, L"; <<LANGUAGES>>", langBlock);

    // ── Replace the "; <<TYPES>>" and "; <<COMPONENTS>>" markers ─────────────
    // Only emit [Types] and [Components] sections when component-based install is
    // active and at least some types / components are defined.
    {
        std::wstring typesBlock;
        if (!types.empty()) {
            typesBlock = L"[Types]\r\n";
            typesBlock += BuildTypesSection(types);
            typesBlock += L"\r\n";
        }
        ReplaceAll(tmpl, L"; <<TYPES>>", typesBlock);

        std::wstring compsBlock;
        if (!comps.empty()) {
            compsBlock = L"[Components]\r\n";
            compsBlock += BuildComponentsSection(comps);
            compsBlock += L"\r\n";
        }
        ReplaceAll(tmpl, L"; <<COMPONENTS>>", compsBlock);
    }

    // ── Replace the "; <<PATH_REGISTRY>>" marker ──────────────────────────────
    // For each folder in pathFolders, append a [Registry] entry that appends it
    // to the system PATH (HKLM). Requires admin privileges (matches PrivilegesRequired=admin).
    std::wstring pathRegBlock;
    for (const auto& folder : cfg.pathFolders) {
        if (folder.empty()) continue;
        pathRegBlock +=
            L"Root: HKLM; Subkey: \"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment\"; "
            L"ValueType: expandsz; ValueName: \"Path\"; ValueData: \"{olddata};" + folder + L"\"; "
            L"Flags: preservestringtype\r\n";
    }
    ReplaceAll(tmpl, L"; <<PATH_REGISTRY>>", pathRegBlock);

    // ── Replace the "; <<FILE_ASSOCIATIONS>>" marker ──────────────────────────
    // For each enabled FileAssocRow, emit HKCR [Registry] entries that register
    // the file extension, ProgID, optional icon, and shell verb commands.
    std::wstring faBlock;
    for (const FileAssocRow& r : assocs) {
        if (!r.enabled) continue;

        std::wstring ext = r.extension;
        if (ext.empty()) continue;

        // Ensure the extension starts with a dot.
        if (ext[0] != L'.') ext = L"." + ext;

        // Derive ProgID: use explicit one, or "AppName.ext" (no leading dot).
        std::wstring pid = r.prog_id;
        if (pid.empty()) {
            std::wstring extNoDoc = ext.substr(1);  // strip leading dot
            pid = proj.name + L"." + extNoDoc;
        }

        // Helper: one Registry line.
        auto Line = [&](const std::wstring& subkey,
                        const std::wstring& valName,
                        const std::wstring& valData,
                        const std::wstring& flags) -> std::wstring
        {
            std::wstring line = L"Root: HKCR; Subkey: \"" + subkey + L"\";"
                L" ValueType: string;";
            if (!valName.empty())
                line += L" ValueName: \"" + valName + L"\";";
            line += L" ValueData: \"" + valData + L"\"; Flags: " + flags + L"\r\n";
            return line;
        };

        faBlock += L"; File association: " + ext + L"\r\n";

        // Extension → ProgID mapping.
        faBlock += Line(ext, L"", pid, L"uninsdeletevalue");

        // Content type (MIME).
        if (!r.content_type.empty())
            faBlock += Line(ext, L"Content Type", r.content_type, L"uninsdeletevalue");

        // ProgID description.
        faBlock += Line(pid, L"", r.description, L"uninsdeletekeyifempty");

        // Default icon.
        if (!r.icon_path.empty()) {
            std::wstring iconData = r.icon_path + L"," + std::to_wstring(r.icon_index);
            faBlock += Line(pid + L"\\DefaultIcon", L"", iconData, L"uninsdeletevalue");
        }

        // Shell verb commands.
        if (!r.open_cmd.empty())
            faBlock += Line(pid + L"\\shell\\open\\command",  L"", r.open_cmd,  L"uninsdeletevalue");
        if (!r.edit_cmd.empty())
            faBlock += Line(pid + L"\\shell\\edit\\command",  L"", r.edit_cmd,  L"uninsdeletevalue");
        if (!r.print_cmd.empty())
            faBlock += Line(pid + L"\\shell\\print\\command", L"", r.print_cmd, L"uninsdeletevalue");

        faBlock += L"\r\n";
    }
    ReplaceAll(tmpl, L"; <<FILE_ASSOCIATIONS>>", faBlock);

    // ── Replace ; <<SETUP_LOG_PROC>> and ; <<SETUP_LOG_CALL>> markers ───────────────────────
    // When logging is enabled AND a destination path can be formed, generate a
    // CopySetupLog() Pascal procedure (inserted before DeinitializeSetup) and
    // its call site inside DeinitializeSetup.  The log is copied from {log}
    // (Inno's temp log path) to the configured destination after setup finishes.
    {
        std::wstring logProc;
        std::wstring logCall;
        if (cfg.setupLogging) {
            std::wstring folder = StripTrailingSep(cfg.setupLogFolder);
            std::wstring fname  = cfg.setupLogFilename;
            std::wstring dst;
            if (!folder.empty() && !fname.empty())
                dst = folder + L"\\" + fname;
            else if (!folder.empty())
                dst = folder + L"\\setup.log";
            else if (!fname.empty())
                dst = fname;
            if (!dst.empty()) {
                std::wstring dstEsc = EscapePascalString(dst);
                if (cfg.setupLogMode == 1) {
                    // Append mode: read existing log, concatenate new log, write back.
                    logProc =
                        L"procedure CopySetupLog();\r\n"
                        L"var\r\n"
                        L"  Src, Dst, Content, OldContent: String;\r\n"
                        L"begin\r\n"
                        L"  Src := ExpandConstant('{log}');\r\n"
                        L"  Dst := '" + dstEsc + L"';\r\n"
                        L"  if not FileExists(Src) then Exit;\r\n"
                        L"  if not LoadStringFromFile(Src, Content) then Exit;\r\n"
                        L"  if FileExists(Dst) then\r\n"
                        L"    if LoadStringFromFile(Dst, OldContent) then\r\n"
                        L"      Content := OldContent + Content;\r\n"
                        L"  SaveStringToFile(Dst, Content, False);\r\n"
                        L"end;\r\n";
                } else {
                    // Overwrite mode: copy log to destination, replacing any existing file.
                    logProc =
                        L"procedure CopySetupLog();\r\n"
                        L"begin\r\n"
                        L"  if FileExists(ExpandConstant('{log}')) then\r\n"
                        L"    FileCopy(ExpandConstant('{log}'), '" + dstEsc + L"', False);\r\n"
                        L"end;\r\n";
                }
                logCall = L"CopySetupLog();";
            }
        }
        ReplaceAll(tmpl, L"; <<SETUP_LOG_PROC>>", logProc);
        ReplaceAll(tmpl, L"; <<SETUP_LOG_CALL>>", logCall);
    }

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
