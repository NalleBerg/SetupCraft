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
#include "dialogs.h"
#include "shortcuts.h"  // SCT_* constants
#include "scripts.h"    // SWR_* constants
#include <algorithm>
#include <map>
#include <vector>

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

// Escape single-quote characters in s for embedding in a Pascal string literal.
// In Pascal, a single quote inside a string is represented as two consecutive quotes.
static std::wstring EscPascal(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size() + 4);
    for (wchar_t c : s) {
        out += c;
        if (c == L'\'') out += L'\'';
    }
    return out;
}

// Remove physical CR/LF characters from an RTF blob before embedding it in a
// Pascal string literal. RTF control words are whitespace-tolerant, so this
// keeps the emitted script compact without changing the rendered content.
static std::wstring CompactRtf(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (c != L'\r' && c != L'\n') out += c;
    }
    return out;
}

static std::wstring StripRtfPictureBlocks(const std::wstring& rtf)
{
    std::wstring out;
    out.reserve(rtf.size());
    for (std::size_t i = 0; i < rtf.size();) {
        if (i + 5 < rtf.size() && rtf[i] == L'{' && rtf[i + 1] == L'\\' &&
            rtf.compare(i + 2, 4, L"pict") == 0) {
            int depth = 0;
            std::size_t j = i;
            while (j < rtf.size()) {
                if (rtf[j] == L'{') ++depth;
                else if (rtf[j] == L'}') {
                    --depth;
                    if (depth == 0) {
                        ++j;
                        break;
                    }
                }
                ++j;
            }
            i = j;
            continue;
        }
        out.push_back(rtf[i]);
        ++i;
    }
    return out;
}

static bool IsHexDigit(wchar_t c)
{
    return (c >= L'0' && c <= L'9') ||
           (c >= L'a' && c <= L'f') ||
           (c >= L'A' && c <= L'F');
}

    std::wstring rtf = StripRtfPictureBlocks(CompactRtf(welcomeRtf));
{
    if (c >= L'0' && c <= L'9') return (int)(c - L'0');
    if (c >= L'a' && c <= L'f') return 10 + (int)(c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (int)(c - L'A');
    return -1;
}

static bool ExtractPngBytesFromRtf(const std::wstring& rtf,
                                   std::vector<unsigned char>& bytes)
{
    static const std::wstring kPngSig = L"89504e470d0a1a0a";
    std::size_t pos = rtf.find(kPngSig);
    if (pos == std::wstring::npos)
        return false;

    std::wstring hex;
    for (std::size_t i = pos; i < rtf.size(); ++i) {
        wchar_t c = rtf[i];
        if (IsHexDigit(c)) {
            hex += c;
            continue;
        }
        break;
    }

    if (hex.size() < 16 || (hex.size() % 2) != 0)
        return false;

    bytes.clear();
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = HexDigitValue(hex[i]);
        int lo = HexDigitValue(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        bytes.push_back((unsigned char)((hi << 4) | lo));
    }
    return !bytes.empty();
}

static bool WriteBinaryFile(const std::wstring& path,
                            const std::vector<unsigned char>& bytes)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(hFile);
    return ok && written == bytes.size();
}

// Build the custom welcome page procedure so the final installer matches the
// Dialogs -> Preview welcome screen rather than Inno's stock wizard page.
static std::wstring BuildWelcomePageCode(const std::wstring& welcomeRtf,
                                         const std::wstring& welcomeImageFile)
{
    if (welcomeRtf.empty()) return L"";

    std::wstring rtf = CompactRtf(welcomeRtf);
    std::wstring code;
    code += L"procedure CreateWelcomePage_SC;\r\n";
    code += L"var\r\n";
    code += L"  WelcomeImage_SC: TBitmapImage;\r\n";
    code += L"begin\r\n";
    code += L"  WelcomePage_SC := CreateCustomPage(wpWelcome, 'Welcome', '');\r\n";
    if (!welcomeImageFile.empty()) {
        code += L"  ExtractTemporaryFile('" + EscPascal(welcomeImageFile) + L"');\r\n";
        code += L"  WelcomeImage_SC := TBitmapImage.Create(WelcomePage_SC);\r\n";
        code += L"  WelcomeImage_SC.Parent := WelcomePage_SC.Surface;\r\n";
        code += L"  WelcomeImage_SC.AutoSize := True;\r\n";
        code += L"  WelcomeImage_SC.PngImage.LoadFromFile(ExpandConstant('{tmp}\\" + EscPascal(welcomeImageFile) + L"'));\r\n";
        code += L"  WelcomeImage_SC.Left := (WelcomePage_SC.SurfaceWidth - WelcomeImage_SC.Width) div 2;\r\n";
        code += L"  WelcomeImage_SC.Top := ScaleY(56);\r\n";
    }
    code += L"  WelcomeViewer_SC := TRichEditViewer.Create(WelcomePage_SC);\r\n";
    code += L"  WelcomeViewer_SC.Parent := WelcomePage_SC.Surface;\r\n";
    code += L"  WelcomeViewer_SC.Left := 0;\r\n";
    code += L"  WelcomeViewer_SC.Top := 0;\r\n";
    code += L"  WelcomeViewer_SC.Width := WelcomePage_SC.SurfaceWidth;\r\n";
    code += L"  WelcomeViewer_SC.Height := WelcomePage_SC.SurfaceHeight;\r\n";
    code += L"  WelcomeViewer_SC.BevelKind := bkFlat;\r\n";
    code += L"  WelcomeViewer_SC.BorderStyle := bsNone;\r\n";
    code += L"  WelcomeViewer_SC.ReadOnly := True;\r\n";
    code += L"  WelcomeViewer_SC.ScrollBars := ssVertical;\r\n";
    code += L"  WelcomeViewer_SC.UseRichEdit := True;\r\n";
    code += L"  WelcomeViewer_SC.RTFText := '" + EscPascal(rtf) + L"';\r\n";
    code += L"end;\r\n";
    return code;
}

// Build the Inno [Code] Pascal block that enforces inter-component dependencies
// at install time via WizardForm.ComponentsList.OnClickCheck.
//
// When component A depends on component B:
//   - Checking A also checks B (so the dep is always present).
//   - Unchecking B also unchecks A (so A is never installed without B).
//
// Returns an empty string when no component has any dependencies (no [Code]
// additions needed in that case).
static std::wstring BuildDepEnforceCode(const std::vector<ComponentRow>& comps,
                                       bool includeWelcomeChromeToggle)
{
    // Build id→component map for dep resolution.
    std::map<int, const ComponentRow*> idToComp;
    for (const auto& c : comps)
        if (c.id > 0) idToComp[c.id] = &c;

    // Collect enforcement rules as Pascal statements.
    std::wstring rules;
    for (const auto& c : comps) {
        if (c.is_required || c.display_name.empty() || c.dependencies.empty()) continue;
        // Description is what Inno shows in the component list (ItemCaption / Items[i]).
        std::wstring descA = EscPascal(c.description.empty() ? c.display_name : c.description);
        for (int depId : c.dependencies) {
            auto it = idToComp.find(depId);
            if (it == idToComp.end()) continue;
            const ComponentRow* dep = it->second;
            // Required comps are always installed silently — no list entry to enforce.
            if (dep->is_required || dep->display_name.empty()) continue;
            std::wstring descB = EscPascal(dep->description.empty() ? dep->display_name : dep->description);
            rules += L"  { '" + c.display_name + L"' requires '" + dep->display_name + L"' }\r\n";
            rules += L"  IdxA := GetCompIdx_SC('" + descA + L"');\r\n";
            rules += L"  IdxB := GetCompIdx_SC('" + descB + L"');\r\n";
            rules += L"  if (IdxA >= 0) and (IdxB >= 0) then\r\n";
            rules += L"  begin\r\n";
            rules += L"    if WizardForm.ComponentsList.Checked[IdxA] then\r\n";
            rules += L"      WizardForm.ComponentsList.Checked[IdxB] := True;\r\n";
            rules += L"    if not WizardForm.ComponentsList.Checked[IdxB] then\r\n";
            rules += L"      WizardForm.ComponentsList.Checked[IdxA] := False;\r\n";
            rules += L"  end;\r\n";
        }
    }

    if (rules.empty() && !includeWelcomeChromeToggle) return L"";

    std::wstring code;
    code += L"{ Component dependency enforcement (generated by SetupCraft) }\r\n";
    code += L"function GetCompIdx_SC(const Desc: String): Integer;\r\n";
    code += L"var I: Integer;\r\n";
    code += L"begin\r\n";
    code += L"  Result := -1;\r\n";
    code += L"  for I := 0 to WizardForm.ComponentsList.Items.Count - 1 do\r\n";
    code += L"    if CompareText(WizardForm.ComponentsList.Items[I], Desc) = 0 then\r\n";
    code += L"    begin\r\n";
    code += L"      Result := I;\r\n";
    code += L"      Exit;\r\n";
    code += L"    end;\r\n";
    code += L"end;\r\n\r\n";
    code += L"procedure EnforceCompDeps_SC;\r\n";
    code += L"var IdxA, IdxB: Integer;\r\n";
    code += L"begin\r\n";
    code += rules;
    code += L"end;\r\n\r\n";
    code += L"procedure OnCompListClickCheck_SC(Sender: TObject);\r\n";
    code += L"begin\r\n";
    code += L"  EnforceCompDeps_SC;\r\n";
    code += L"end;\r\n\r\n";
    code += L"procedure CurPageChanged(CurPageID: Integer);\r\n";
    code += L"begin\r\n";
    code += L"  if CurPageID = wpSelectComponents then\r\n";
    code += L"    WizardForm.ComponentsList.OnClickCheck := @OnCompListClickCheck_SC;\r\n";
    if (includeWelcomeChromeToggle) {
        code += L"  if Assigned(WelcomePage_SC) then\r\n";
        code += L"    WizardForm.WizardSmallBitmapImage.Visible := CurPageID <> WelcomePage_SC.ID;\r\n";
    }
    code += L"end;\r\n\r\n";
    return code;
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

// Recursively sum on-disk file sizes for all files under a directory tree.
// Used at .iss generation time to embed the required disk space in the installer script.
static long long CalculateDirSize(const std::wstring& dir)
{
    long long total = 0;
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            total += CalculateDirSize(dir + L"\\" + fd.cFileName);
        } else {
            total += ((long long)fd.nFileSizeHigh << 32) | (long long)fd.nFileSizeLow;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return total;
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
        {
            std::wstring flags;
            if (c.is_fixed) flags = L"fixed";
            if (c.is_exclusive) { if (!flags.empty()) flags += L" "; flags += L"exclusive"; }
            if (c.is_restart)   { if (!flags.empty()) flags += L" "; flags += L"restart"; }
            if (!flags.empty()) out += L"; Flags: " + flags;
        }
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

// Map a destination_path root segment to the corresponding Inno Setup directory constant.
// destination_path is stored as "Root\sub\path" where Root is one of the four tree roots.
// Returns {innoConst, subPath} — e.g. {"app", "sub\\path"} or {"app", ""} for root-level files.
static std::pair<std::wstring, std::wstring> MapDestDir(const std::wstring& destPath)
{
    // Helper: strip leading backslash from sub-path.
    auto strip = [](std::wstring s) -> std::wstring {
        if (!s.empty() && s.front() == L'\\') s = s.substr(1);
        return s;
    };

    struct { const wchar_t* prefix; const wchar_t* innoConst; } kMap[] = {
        { L"Program Files",    L"{app}"            },
        { L"ProgramData",      L"{commonappdata}"  },
        { L"AppData (Roaming)",L"{userappdata}"    },
        { L"AskAtInstall",     L"{app}"            },  // user-chosen at install time
    };
    for (const auto& m : kMap) {
        std::wstring pfx(m.prefix);
        if (destPath.size() >= pfx.size() &&
            _wcsnicmp(destPath.c_str(), pfx.c_str(), pfx.size()) == 0) {
            std::wstring sub = destPath.substr(pfx.size());
            if (!sub.empty() && sub.front() == L'\\') sub = sub.substr(1);
            return { m.innoConst, sub };
        }
    }
    // Fallback: treat the whole path as relative to {app}.
    return { L"{app}", destPath };
}

// Build the [Files] section body from the per-file DB rows.
// Each FileRow with a non-empty source_path and install_scope != "__folder__"
// becomes one Source: line.  Folder sentinel rows (install_scope == "__folder__")
// are skipped — Inno creates directories automatically.
static std::wstring BuildFilesSection(const std::vector<FileRow>& files)
{
    std::wstring out;
    for (const auto& f : files) {
        // Skip virtual folder sentinels and rows with no real source file.
        if (f.source_path.empty()) continue;
        if (f.install_scope == L"__folder__") continue;

        // Destination directory.
        auto [innoConst, sub] = MapDestDir(f.destination_path);
        std::wstring destDir = innoConst;
        if (!sub.empty()) destDir += L"\\" + sub;

        // Override wins if set.
        if (!f.dest_dir_override.empty()) destDir = f.dest_dir_override;

        out += L"Source: \"" + f.source_path + L"\"; DestDir: \"" + destDir + L"\"";

        // Append developer-specified Inno flags if any.
        if (!f.inno_flags.empty())
            out += L"; Flags: " + f.inno_flags;

        out += L"\r\n";
    }
    return out;
}

// Mutex message translations — one entry per ISL base name (lowercased for lookup).
// "Default" maps to "english" so it is stored under that key.
// Only languages actually selected by the developer are emitted in [CustomMessages].
static const std::pair<const wchar_t*, const wchar_t*> kMutexMessages[] = {
    { L"english",    L"Another copy of this installer is already running.%n%nYou can close the running installer to continue, close this installer to let the other run, or cancel." },
    { L"catalan",    L"Una altra còpia d'aquest instal·lador ja s'està executant.%n%nPodeu tancar l'instal·lador en execució per continuar, tancar aquest instal·lador per deixar l'altre continuar, o cancel·lar." },
    { L"czech",      L"Jiná kopie tohoto instalačního programu již běží.%n%nMůžete zavřít spuštěný instalační program a pokračovat, zavřít tento instalační program a nechat ten druhý běžet, nebo zrušit." },
    { L"danish",     L"En anden kopi af dette installationsprogram kører allerede.%n%nDu kan lukke det kørende installationsprogram for at fortsætte, lukke dette installationsprogram og lade det andet køre, eller annullere." },
    { L"dutch",      L"Een andere kopie van dit installatieprogramma wordt al uitgevoerd.%n%nU kunt het actieve installatieprogramma sluiten om door te gaan, dit installatieprogramma sluiten om het andere te laten doorgaan, of annuleren." },
    { L"finnish",    L"Toinen kopio tästä asennusohjelmasta on jo käynnissä.%n%nVoit sulkea käynnissä olevan asennusohjelman jatkaaksesi, sulkea tämän asennusohjelman antaaksesi toisen jatkua, tai peruuttaa." },
    { L"french",     L"Une autre copie de ce programme d'installation est déjà en cours d'exécution.%n%nVous pouvez fermer le programme d'installation en cours pour continuer, fermer ce programme pour laisser l'autre continuer, ou annuler." },
    { L"german",     L"Eine andere Kopie dieses Installationsprogramms wird bereits ausgeführt.%n%nSie können das laufende Installationsprogramm schließen, um fortzufahren, dieses schließen, um das andere weiterlaufen zu lassen, oder abbrechen." },
    { L"hebrew",     L"עותק אחר של מתקין זה כבר פועל.%n%nתוכל לסגור את המתקין הפועל כדי להמשיך, לסגור את מתקין זה כדי לאפשר לאחר להמשיך, או לבטל." },
    { L"hungarian",  L"A telepítő egy másik példánya már fut.%n%nA futó telepítőt bezárhatja a folytatáshoz, bezárhatja ezt a telepítőt, hogy a másik futhasson, vagy megszakíthatja." },
    { L"italian",    L"È già in esecuzione un'altra copia di questo programma di installazione.%n%nÈ possibile chiudere il programma in esecuzione per continuare, chiudere questo per lasciare in esecuzione l'altro, oppure annullare." },
    { L"japanese",   L"このインストーラーの別のコピーが既に実行中です。%n%n実行中のインストーラーを閉じて続行するか、このインストーラーを閉じてもう一方を続行させるか、またはキャンセルすることができます。" },
    { L"norwegian",  L"En annen kopi av dette installasjonsprogrammet kjører allerede.%n%nDu kan lukke det kjørende installasjonsprogrammet for å fortsette, lukke dette og la det andre kjøre, eller avbryte." },
    { L"polish",     L"Inna kopia tego instalatora jest już uruchomiona.%n%nMożesz zamknąć uruchomiony instalator, aby kontynuować, zamknąć ten instalator i pozwolić drugiemu działać, lub anulować." },
    { L"portuguese", L"Outra cópia deste instalador já está em execução.%n%nPode fechar o instalador em execução para continuar, fechar este instalador para deixar o outro continuar, ou cancelar." },
    { L"slovak",     L"Iná kópia tohto inštalačného programu už beží.%n%nMôžete zatvoriť spustený inštalačný program a pokračovať, zatvoriť tento a nechať ten druhý bežať, alebo zrušiť." },
    { L"slovenian",  L"Druga kopija tega namestitvenega programa že deluje.%n%nLahko zaprete zagnani namestitveni program in nadaljujete, zaprete ta in pustite drugega delovati, ali pa prekličete." },
    { L"spanish",    L"Ya se está ejecutando otra copia de este instalador.%n%nPuede cerrar el instalador en ejecución para continuar, cerrar este instalador y dejar que el otro continúe, o cancelar." },
    { L"swedish",    L"En annan kopia av det här installationsprogrammet körs redan.%n%nDu kan stänga installationsprogrammet som körs för att fortsätta, stänga det här för att låta det andra fortsätta, eller avbryta." },
    { L"turkish",    L"Bu yükleyicinin başka bir kopyası zaten çalışıyor.%n%nDevam etmek için çalışan yükleyiciyi kapatabilir, diğerinin devam etmesi için bu yükleyiciyi kapatabilir veya iptal edebilirsiniz." },
    { L"ukrainian",  L"Інша копія цього інсталятора вже запущена.%n%nВи можете закрити запущений інсталятор, щоб продовжити, закрити цей інсталятор, щоб дозволити іншому продовжити, або скасувати." },
};

// Build the [CustomMessages] body for the selected languages only.
// Emits a mutex_message line for each selected language that has a known translation.
// The comment header is included so the output is self-documenting in the generated file.
static std::wstring BuildCustomMessagesSection(
    const std::vector<InnoLangEntry>& langs)
{
    // Build a lookup map from the table above.
    std::map<std::wstring, std::wstring> msgMap;
    for (const auto& kv : kMutexMessages)
        msgMap[kv.first] = kv.second;

    std::wstring out;
    out += L"; Shown when the named mutex is already held — installer is already running.\r\n";
    out += L"; Three choices: Yes = close the running one, No = close this one, Cancel = do nothing.\r\n";
    for (const auto& lang : langs) {
        std::wstring name = (lang.isl == L"Default") ? L"english" : ToLower(lang.isl);
        auto it = msgMap.find(name);
        if (it != msgMap.end())
            out += name + L".mutex_message=" + it->second + L"\r\n";
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

// Return the final path component from a file path.
static std::wstring FileNameFromPath(const std::wstring& p)
{
    std::size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? p : p.substr(pos + 1);
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

// Build a [Tasks] section body for desktop/pin opt-out tasks.
// A task is only emitted when the user enabled the corresponding opt-out flag.
// Returns empty string when no opt-out tasks are needed.
static std::wstring BuildTasksSection(
    const std::vector<DB::ScShortcutRow>& shortcuts,
    bool desktopOptOut, bool smPinOptOut, bool tbPinOptOut)
{
    // Determine whether any desktop / pin shortcuts actually exist.
    bool hasDesktop    = false;
    bool hasSmPin      = false;
    bool hasTbPin      = false;
    for (const auto& sc : shortcuts) {
        if (sc.type == SCT_DESKTOP)     hasDesktop = true;
        if (sc.type == SCT_PIN_START)   hasSmPin   = true;
        if (sc.type == SCT_PIN_TASKBAR) hasTbPin   = true;
    }

    std::wstring tasks;
    if (hasDesktop && desktopOptOut)
        tasks += L"Name: \"desktopicon\"; Description: \"Create a &desktop icon\"; "
                 L"GroupDescription: \"Additional icons:\"; Flags: unchecked\r\n";
    if (hasSmPin && smPinOptOut)
        tasks += L"Name: \"pinstart\"; Description: \"&Pin to Start\"; "
                 L"GroupDescription: \"Start Menu:\"; Flags: unchecked\r\n";
    if (hasTbPin && tbPinOptOut)
        tasks += L"Name: \"pintaskbar\"; Description: \"Pin to &Taskbar\"; "
                 L"GroupDescription: \"Taskbar:\"; Flags: unchecked\r\n";

    if (tasks.empty()) return L"";
    return L"[Tasks]\r\n" + tasks + L"\r\n";
}

// Resolve the Start Menu path for a shortcut, building the {group}\subfolder\...
// chain from the node tree.  Returns L"{group}" for a shortcut at the Programs root.
static std::wstring ResolveSmPath(
    const DB::ScShortcutRow& sc,
    const std::vector<DB::ScMenuNodeRow>& nodes)
{
    // Build id → node map for quick look-up.
    std::map<int, const DB::ScMenuNodeRow*> byId;
    for (const auto& n : nodes) byId[n.id] = &n;

    // Walk up the parent chain to collect folder names.
    std::vector<std::wstring> parts;
    int cur = sc.sm_node_id;
    while (cur > 1) {  // 0=SM root, 1=Programs; both map to {group}
        auto it = byId.find(cur);
        if (it == byId.end()) break;
        parts.push_back(it->second->name);
        cur = it->second->parent_id;
    }

    // Reverse so topmost folder comes first.
    std::wstring path = L"{group}";
    for (int i = (int)parts.size() - 1; i >= 0; --i)
        path += L"\\" + parts[i];
    return path;
}

// Build the [Icons] section body from shortcut definitions.
// Desktop shortcuts → {commondesktop}\name when the installer runs as admin.
// SM shortcuts      → {group}\[subfolder\...]name
// Pin shortcuts are handled via [Run] instead.
static std::wstring BuildIconsSection(
    const std::vector<DB::ScShortcutRow>& shortcuts,
    const std::vector<DB::ScMenuNodeRow>& nodes,
    bool desktopOptOut)
{
    std::wstring out;
    for (const auto& sc : shortcuts) {
        if (sc.type != SCT_DESKTOP && sc.type != SCT_STARTMENU) continue;
        if (sc.exe_path.empty() && sc.name.empty()) continue;

        std::wstring dest;
        std::wstring taskFlag;
        if (sc.type == SCT_DESKTOP) {
            dest = L"{commondesktop}\\" + sc.name;
            if (desktopOptOut) taskFlag = L"desktopicon";
        } else {
            dest = ResolveSmPath(sc, nodes) + L"\\" + sc.name;
        }

        out += L"Name: \"" + dest + L"\";"
               L" Filename: \"" + sc.exe_path + L"\"";

        if (!sc.working_dir.empty())
            out += L"; WorkingDir: \"" + sc.working_dir + L"\"";
        if (!sc.arguments.empty())
            out += L"; Parameters: \"" + sc.arguments + L"\"";
        if (!sc.comment.empty())
            out += L"; Comment: \"" + sc.comment + L"\"";
        if (!sc.hotkey.empty())
            out += L"; HotKey: \"" + sc.hotkey + L"\"";
        if (!sc.icon_path.empty())
            out += L"; IconFilename: \"" + sc.icon_path + L"\"; IconIndex: "
                   + std::to_wstring(sc.icon_index);
        if (sc.run_as_admin)
            out += L"; Flags: runasadmin";
        if (!taskFlag.empty())
            out += (sc.run_as_admin ? L" " : L"; Flags: ") +
                   (sc.run_as_admin ? taskFlag : (L"uncheckedonce; Tasks: " + taskFlag));

        out += L"\r\n";
    }
    if (out.empty()) return L"";
    return L"[Icons]\r\n" + out + L"\r\n";
}

// Build the [Run] section body.
// Includes:
//   1. Script SWR_AFTER_FILES entries (unconditional).
//   2. Script SWR_FINISH_OPTOUT entries (Flags: postinstall skipifsilent).
//   3. Finish-page app launch entry (from IDLG_GetFinishLaunchEnabled()).
//   4. Pin-to-Start / Pin-to-Taskbar entries via PowerShell.
static std::wstring BuildRunSection(
    const std::vector<DB::ScriptRow>& scripts,
    const std::vector<DB::ScShortcutRow>& shortcuts,
    bool smPinOptOut, bool tbPinOptOut)
{
    std::wstring out;

    // SWR_AFTER_FILES scripts (run unconditionally after file copy).
    for (const auto& s : scripts) {
        if (s.when_to_run != SWR_AFTER_FILES) continue;
        // Write script content to a temp file at runtime and run it.
        // For now emit a Run entry that executes the script directly.
        // Script type 0=BAT, 1=PS1.
        std::wstring flags;
        if (s.run_hidden)  flags += L"runhidden ";
        if (!s.wait_for_completion) flags += L"nowait ";
        if (!s.required_components.empty())
            flags += L"; Components: \"" + s.required_components + L"\"";
        if (s.type == SCR_TYPE_PS1) {
            // Emit inline PS1 call: write content to %TEMP% file, call PowerShell.
            out += L"; Script: " + s.name + L"\r\n";
            out += L"Filename: \"{sys}\\WindowsPowerShell\\v1.0\\powershell.exe\";"
                   L" Parameters: \"-ExecutionPolicy Bypass -File \\\"{tmp}\\" + s.name + L".ps1\\\"\"";
        } else {
            out += L"; Script: " + s.name + L"\r\n";
            out += L"Filename: \"{cmd}\"; Parameters: \"/c \\\"{tmp}\\" + s.name + L".bat\\\"\"";
        }
        if (!flags.empty()) out += L"; Flags: " + flags;
        out += L"\r\n";
    }

    // SWR_FINISH_OPTOUT scripts (user opt-out checkbox on Finish page).
    for (const auto& s : scripts) {
        if (s.when_to_run != SWR_FINISH_OPTOUT) continue;
        std::wstring desc = s.description.empty() ? s.name : s.description;
        std::wstring flags = L"postinstall skipifsilent";
        if (s.run_hidden)              flags += L" runhidden";
        if (!s.wait_for_completion)    flags += L" nowait";
        if (s.finish_checked_by_default == 0) flags += L" unchecked";
        if (s.type == SCR_TYPE_PS1) {
            out += L"; Script: " + s.name + L"\r\n";
            out += L"Filename: \"{sys}\\WindowsPowerShell\\v1.0\\powershell.exe\";"
                   L" Parameters: \"-ExecutionPolicy Bypass -File \\\"{tmp}\\" + s.name + L".ps1\\\"\";";
        } else {
            out += L"; Script: " + s.name + L"\r\n";
            out += L"Filename: \"{cmd}\"; Parameters: \"/c \\\"{tmp}\\" + s.name + L".bat\\\"\";";
        }
        out += L" Description: \"" + desc + L"\"; Flags: " + flags + L"\r\n";
    }

    // Finish-page app launch entry from the Dialogs page.
    if (IDLG_GetFinishLaunchEnabled()) {
        std::wstring desc = IDLG_GetFinishLaunchDesc();
        std::wstring flags = L"nowait postinstall shellexec skipifsilent";
        if (!IDLG_GetFinishLaunchDefaultChecked()) flags += L" unchecked";
        out += L"Filename: \"{app}\\{#ExeName}\"; Description: \"" + desc + L"\"; Flags: " + flags + L"\r\n";
    }

    // Pin to Start / Pin to Taskbar via PowerShell (requires Windows 10+).
    for (const auto& sc : shortcuts) {
        if (sc.type == SCT_PIN_START) {
            std::wstring flags = L"nowait runhidden shellexec";
            if (smPinOptOut) flags += L" unchecked; Tasks: \"pinstart\"";
            out += L"Filename: \"{sys}\\WindowsPowerShell\\v1.0\\powershell.exe\";"
                   L" Parameters: \"-ExecutionPolicy Bypass -Command \\\"$shell = New-Object -COM Shell.Application;"
                   L" $folder = $shell.NameSpace(Split-Path '" + sc.exe_path + L"');"
                   L" $item = $folder.ParseName(Split-Path '" + sc.exe_path + L"' -Leaf);"
                   L" $item.InvokeVerb('taskbarpin')\\\"\"; Flags: " + flags + L"\r\n";
        } else if (sc.type == SCT_PIN_TASKBAR) {
            std::wstring flags = L"nowait runhidden shellexec";
            if (tbPinOptOut) flags += L" unchecked; Tasks: \"pintaskbar\"";
            out += L"Filename: \"{sys}\\WindowsPowerShell\\v1.0\\powershell.exe\";"
                   L" Parameters: \"-ExecutionPolicy Bypass -Command \\\"$shell = New-Object -COM Shell.Application;"
                   L" $folder = $shell.NameSpace(Split-Path '" + sc.exe_path + L"');"
                   L" $item = $folder.ParseName(Split-Path '" + sc.exe_path + L"' -Leaf);"
                   L" $item.InvokeVerb('taskbarpin')\\\"\"; Flags: " + flags + L"\r\n";
        }
    }

    if (out.empty()) return L"";
    return L"[Run]\r\n" + out + L"\r\n";
}

// Build the [UninstallRun] section body from scripts tagged SWR_UNINSTALL
// or with also_uninstall == 1.
static std::wstring BuildUninstallRunSection(
    const std::vector<DB::ScriptRow>& scripts)
{
    std::wstring out;
    for (const auto& s : scripts) {
        if (s.when_to_run != SWR_UNINSTALL && !s.also_uninstall) continue;
        std::wstring flags;
        if (s.run_hidden)           flags += L"runhidden ";
        if (!s.wait_for_completion) flags += L"nowait ";
        if (s.type == SCR_TYPE_PS1) {
            out += L"; Script: " + s.name + L"\r\n";
            out += L"Filename: \"{sys}\\WindowsPowerShell\\v1.0\\powershell.exe\";"
                   L" Parameters: \"-ExecutionPolicy Bypass -File \\\"{tmp}\\" + s.name + L".ps1\\\"\"";
        } else {
            out += L"; Script: " + s.name + L"\r\n";
            out += L"Filename: \"{cmd}\"; Parameters: \"/c \\\"{tmp}\\" + s.name + L".bat\\\"\"";
        }
        if (!flags.empty()) out += L"; Flags: " + flags;
        out += L"\r\n";
    }
    if (out.empty()) return L"";
    return L"[UninstallRun]\r\n" + out + L"\r\n";
}

// Build additional custom [Registry] entries from the Registry page.
// Entries with name == "__KEY__" are key-creation-only (no ValueType).
static std::wstring BuildCustomRegistrySection(
    const std::vector<RegistryEntryRow>& entries)
{
    std::wstring out;
    for (const auto& e : entries) {
        // Normalise hive string to Inno format.
        std::wstring hive = e.hive;
        if (hive == L"HKEY_LOCAL_MACHINE")    hive = L"HKLM";
        else if (hive == L"HKEY_CURRENT_USER") hive = L"HKCU";
        else if (hive == L"HKEY_CLASSES_ROOT") hive = L"HKCR";
        else if (hive == L"HKEY_USERS")        hive = L"HKU";

        if (e.name == L"__KEY__") {
            // Key creation only — no ValueType/ValueName/ValueData.
            out += L"Root: " + hive + L"; Subkey: \"" + e.path + L"\"";
        } else {
            // Map type string to Inno ValueType.
            std::wstring vtype;
            if      (e.type == L"dword")    vtype = L"dword";
            else if (e.type == L"binary")   vtype = L"binary";
            else if (e.type == L"expandsz") vtype = L"expandsz";
            else if (e.type == L"multisz")  vtype = L"multisz";
            else                            vtype = L"string";

            out += L"Root: " + hive + L"; Subkey: \"" + e.path + L"\";"
                   L" ValueType: " + vtype + L";";
            if (!e.name.empty())
                out += L" ValueName: \"" + e.name + L"\";";
            out += L" ValueData: \"" + e.data + L"\"";
        }
        if (!e.flags.empty())
            out += L"; Flags: " + e.flags;
        if (!e.components.empty())
            out += L"; Components: \"" + e.components + L"\"";
        out += L"\r\n";
    }
    return out;
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
    const std::vector<ComponentRow>&     comps,
    const IssExtraData&                  extra)
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

    std::wstring genDir = outPath;
    std::size_t genSlash = genDir.find_last_of(L"\\/");
    if (genSlash != std::wstring::npos)
        genDir = genDir.substr(0, genSlash);
    else
        genDir = L".";

    std::wstring welcomeImageFile;
    if (IDLG_IsDialogEnabled(IDLG_WELCOME)) {
        std::vector<unsigned char> welcomePng;
        if (ExtractPngBytesFromRtf(IDLG_GetDialogRtf(IDLG_WELCOME), welcomePng)) {
            welcomeImageFile = FileNameFromPath(outPath);
            std::size_t dot = welcomeImageFile.find_last_of(L'.');
            if (dot != std::wstring::npos)
                welcomeImageFile = welcomeImageFile.substr(0, dot);
            welcomeImageFile += L"_welcome_preview.png";
            if (!WriteBinaryFile(genDir + L"\\" + welcomeImageFile, welcomePng))
                return L"Cannot write welcome preview image: " + genDir + L"\\" + welcomeImageFile;
        }
    }

    // Effective output base filename.
    // Default: strip whitespace from project name, then "<Name>_<Version>_Setup".
    // e.g. "SetupCraft Suite" 1.2.0  →  "SetupCraftSuite_1.2.0_Setup"
    std::wstring outBase = cfg.outputFilename;
    if (outBase.empty()) {
        std::wstring safeName = proj.name;
        safeName.erase(std::remove_if(safeName.begin(), safeName.end(), ::iswspace), safeName.end());
        if (safeName.empty()) {
            outBase = L"Setup";
        } else {
            outBase = safeName + (proj.version.empty() ? L"" : L"_" + proj.version) + L"_Setup";
        }
    }

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

    // Calculate total installed size at generation time for Ready-page disk space display.
    // requiredMB is embedded as a literal in the generated [Code] Pascal functions.
    long long totalBytes = CalculateDirSize(sourceDir);
    long long requiredMB = totalBytes > 0 ? (totalBytes + 1048575LL) / 1048576LL : 0LL;

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
        { L"WizardStyle",      L"modern"                                                        },
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
        // SourceDir is kept in the token table for legacy template lines that may still
        // reference it (e.g. DefaultDirName).  The [Files] section is now generated from
        // the per-file DB rows via ; <<FILES>> instead of a wildcard.
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
        { L"SetupWindowTitle",         IDLG_GetInstallerTitle().empty() ? proj.name : IDLG_GetInstallerTitle() },
        { L"UninstallDisplayName",     cfg.uninstallDisplayName.empty() ? proj.name : cfg.uninstallDisplayName },
        { L"UninstallFilesDir",        cfg.uninstallFilesDir.empty() ? L"{app}" : cfg.uninstallFilesDir },
        { L"LanguageDetectionMethod",  LangDetectionMethodStr(cfg.langDetectionMethod) },
        { L"ShowLanguageDialog",       ShowLanguageDialogStr(cfg.showLanguageDialog)    },

        // Dialog-derived [Setup] tokens
        { L"DisableWelcomePage",       IDLG_IsDialogEnabled(IDLG_WELCOME)  ? L"no" : L"yes" },
        { L"DisableReadyPage",         IDLG_IsDialogEnabled(IDLG_READY)    ? L"no" : L"yes" },
        { L"DisableFinishedPage",      IDLG_IsDialogEnabled(IDLG_FINISH)   ? L"no" : L"yes" },
        { L"AlwaysShowDirOnReadyPage", IDLG_GetReadyShowDir()   ? L"yes" : L"no"             },
        { L"AlwaysShowGroupOnReadyPage", IDLG_GetReadyShowGroup() ? L"yes" : L"no"           },
        { L"LicenseFile",
          (IDLG_GetLicenseSource() == 1 && !IDLG_GetLicenseFilePath().empty())
              ? IDLG_GetLicenseFilePath() : L"" },
    };

    // ── "Line tokens" — emit full Key=Value when non-empty, else empty string ──
    // These replace {#Token} placeholders that represent entire optional lines.
    {
        std::wstring iconPath = IDLG_GetInstallerIconPath();
        tokens[L"SetupIconFileLine"]         = iconPath.empty() ? L"" : L"SetupIconFile=" + iconPath;
        tokens[L"UninstallDisplayIconLine"]  = iconPath.empty() ? L"" : L"UninstallDisplayIcon={app}\\" + FileNameFromPath(iconPath);
        tokens[L"WizardImageFileLine"]       = extra.wizardImageFile.empty()      ? L"" : L"WizardImageFile=" + extra.wizardImageFile;
        tokens[L"WizardSmallImageFileLine"]  = extra.wizardSmallImageFile.empty() ? L"" : L"WizardSmallImageFile=" + extra.wizardSmallImageFile;
    }

    // ── Substitute {#Token} placeholders ─────────────────────────────────────
    for (const auto& kv : tokens)
        ReplaceAll(tmpl, L"{#" + kv.first + L"}", kv.second);

    // ── Replace the "; <<LANGUAGES>>" marker with actual language entries ─────
    std::wstring langBlock = BuildLanguagesSection(langs);
    ReplaceAll(tmpl, L"; <<LANGUAGES>>", langBlock);

    // ── Replace the "; <<CUSTOM_MESSAGES>>" marker ────────────────────────────
    // Only emit mutex_message lines for the languages the developer selected,
    // so no [CustomMessages] prefix is ever an undeclared language.
    std::wstring cmBlock = BuildCustomMessagesSection(langs);
    ReplaceAll(tmpl, L"; <<CUSTOM_MESSAGES>>", cmBlock);

    // ── Replace the "; <<FILES>>" marker ──────────────────────────────────────
    // Build a proper per-file [Files] section from the DB rows instead of a
    // single SourceDir wildcard, so only the files the developer added are included.
    std::wstring filesBlock = BuildFilesSection(extra.files);
    if (!welcomeImageFile.empty()) {
        filesBlock += L"Source: \"" + welcomeImageFile + L"\"; DestDir: \"{tmp}\"; Flags: dontcopy\r\n";
    }
    ReplaceAll(tmpl, L"; <<FILES>>", filesBlock);

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

    // ── Replace the "; <<INSTALL_PROGRESS_CODE>>" marker ────────────────────────────────────
    // Emits Pascal code for smooth progress bar (PBS_SMOOTH), ETA countdown label,
    // and/or folder page read-only (when "Allow folder change" is unchecked).
    {
        const bool smooth          = IDLG_GetInstallProgressSmooth();
        const bool eta             = IDLG_GetInstallShowEta();
        const bool folderReadOnly  = !IDLG_GetSelectFolderAllowChange();
        std::wstring installCode;

        if (smooth || eta || folderReadOnly || IDLG_IsDialogEnabled(IDLG_WELCOME)) {
            if (smooth) {
                installCode +=
                    L"function GetWindowLong(hWnd: LongInt; nIndex: Integer): LongInt;\r\n"
                    L"  external 'GetWindowLongW@user32.dll stdcall';\r\n"
                    L"function SetWindowLong(hWnd: LongInt; nIndex: Integer; dwNewLong: LongInt): LongInt;\r\n"
                    L"  external 'SetWindowLongW@user32.dll stdcall';\r\n"
                    L"\r\n";
            }
            if (eta || IDLG_IsDialogEnabled(IDLG_WELCOME)) {
                installCode +=
                    L"function GetTickCount_SC: Cardinal;\r\n"
                    L"  external 'GetTickCount@kernel32.dll stdcall';\r\n"
                    L"\r\n"
                    L"var\r\n"
                    L"  g_InstallStart_SC: Cardinal;\r\n"
                    L"  g_EtaLabel_SC: TLabel;\r\n"
                    L"  WelcomePage_SC: TWizardPage;\r\n"
                    L"  WelcomeViewer_SC: TRichEditViewer;\r\n"
                    L"\r\n";
            }
            if (IDLG_IsDialogEnabled(IDLG_WELCOME)) {
                installCode += L"procedure CreateWelcomePage_SC; forward;\r\n\r\n";
            }
            installCode += L"procedure InitializeWizard;\r\n";
            if (smooth) {
                installCode +=
                    L"begin\r\n"
                    L"  SetWindowLong(WizardForm.ProgressGauge.Handle, GWL_STYLE,\r\n"
                    L"    GetWindowLong(WizardForm.ProgressGauge.Handle, GWL_STYLE) or PBS_SMOOTH);\r\n";
            } else {
                installCode += L"begin\r\n";
            }
            if (eta) {
                installCode +=
                    L"  g_EtaLabel_SC := TLabel.Create(WizardForm);\r\n"
                    L"  g_EtaLabel_SC.Parent := WizardForm.InstallingPage;\r\n"
                    L"  g_EtaLabel_SC.AutoSize := False;\r\n"
                    L"  g_EtaLabel_SC.Left   := WizardForm.ProgressGauge.Left;\r\n"
                    L"  g_EtaLabel_SC.Top    := WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + 4;\r\n"
                    L"  g_EtaLabel_SC.Width  := WizardForm.ProgressGauge.Width;\r\n"
                    L"  g_EtaLabel_SC.Caption := '';\r\n";
            }
            if (folderReadOnly) {
                installCode +=
                    L"  WizardForm.DirEdit.ReadOnly := True;\r\n"
                    L"  WizardForm.DirBrowseButton.Enabled := False;\r\n";
            }
            if (IDLG_IsDialogEnabled(IDLG_WELCOME)) {
                installCode += L"  CreateWelcomePage_SC;\r\n";
            }
            installCode += L"end;\r\n\r\n";

            if (eta) {
                installCode +=
                    L"procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);\r\n"
                    L"var\r\n"
                    L"  Elapsed, Remaining: Cardinal;\r\n"
                    L"  RemainSec: Integer;\r\n"
                    L"  Pct: Double;\r\n"
                    L"begin\r\n"
                    L"  if CurProgress = 0 then\r\n"
                    L"  begin\r\n"
                    L"    g_InstallStart_SC := GetTickCount_SC;\r\n"
                    L"    if Assigned(g_EtaLabel_SC) then\r\n"
                    L"      g_EtaLabel_SC.Caption := 'Calculating time remaining...';\r\n"
                    L"    Exit;\r\n"
                    L"  end;\r\n"
                    L"  if (MaxProgress <= 0) then Exit;\r\n"
                    L"  Elapsed := GetTickCount_SC - g_InstallStart_SC;\r\n"
                    L"  if Elapsed < 1000 then Exit;\r\n"
                    L"  Pct := CurProgress / MaxProgress;\r\n"
                    L"  if Pct <= 0 then Exit;\r\n"
                    L"  Remaining := Round(Elapsed / Pct) - Elapsed;\r\n"
                    L"  RemainSec := Remaining div 1000;\r\n"
                    L"  if RemainSec < 0 then RemainSec := 0;\r\n"
                    L"  if Assigned(g_EtaLabel_SC) then\r\n"
                    L"    g_EtaLabel_SC.Caption := 'Time remaining: '\r\n"
                    L"      + IntToStr(RemainSec div 60) + ' min '\r\n"
                    L"      + IntToStr(RemainSec mod 60) + ' sec';\r\n"
                    L"end;\r\n\r\n";
            }
        }
        ReplaceAll(tmpl, L"; <<INSTALL_PROGRESS_CODE>>", installCode);
    }

    // ── Replace the "; <<WELCOME_PAGE_CODE>>" marker ───────────────────────────────────────
    // Recreates the Dialogs->Preview welcome screen as the actual installer welcome page.
    {
        std::wstring welcomeCode;
        if (IDLG_IsDialogEnabled(IDLG_WELCOME))
            welcomeCode = BuildWelcomePageCode(IDLG_GetDialogRtf(IDLG_WELCOME), welcomeImageFile);
        ReplaceAll(tmpl, L"; <<WELCOME_PAGE_CODE>>", welcomeCode);
    }

        std::wstring depCode = BuildDepEnforceCode(comps, IDLG_IsDialogEnabled(IDLG_WELCOME));
    // Always emits UpdateReadyMemo (shows required MB and available MB on the Ready page).
    // Emits NextButtonClick only when requiredMB > 0: blocks the install if free space on
    // the target drive is insufficient — user must click Back and pick a different folder.
    {
        std::wstring reqStr = std::to_wstring(requiredMB);
        std::wstring diskCode =
            L"function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;\r\n"
            L"var\r\n"
            L"  Drive: String;\r\n"
            L"  FreeMB, TotalMB: Cardinal;\r\n"
            L"begin\r\n"
            L"  Result := '';\r\n"
            L"  if MemoUserInfoInfo <> '' then Result := Result + MemoUserInfoInfo + NewLine + NewLine;\r\n"
            L"  if MemoDirInfo <> '' then Result := Result + MemoDirInfo + NewLine + NewLine;\r\n"
            L"  if MemoTypeInfo <> '' then Result := Result + MemoTypeInfo + NewLine + NewLine;\r\n"
            L"  if MemoComponentsInfo <> '' then Result := Result + MemoComponentsInfo + NewLine + NewLine;\r\n"
            L"  if MemoGroupInfo <> '' then Result := Result + MemoGroupInfo + NewLine + NewLine;\r\n"
            L"  if MemoTasksInfo <> '' then Result := Result + MemoTasksInfo + NewLine + NewLine;\r\n"
            L"  Drive := ExtractFileDrive(WizardDirValue());\r\n"
            L"  GetSpaceOnDisk(Drive, True, FreeMB, TotalMB);\r\n"
            L"  Result := Result + 'Disk space required:  " + reqStr + L" MB' + NewLine;\r\n"
            L"  Result := Result + 'Disk space available (' + Drive + '): ' + IntToStr(FreeMB) + ' MB';\r\n"
            L"end;\r\n"
            L"\r\n";
        if (requiredMB > 0) {
            diskCode +=
                L"function NextButtonClick(CurPageID: Integer): Boolean;\r\n"
                L"var\r\n"
                L"  Drive: String;\r\n"
                L"  FreeMB, TotalMB: Cardinal;\r\n"
                L"begin\r\n"
                L"  Result := True;\r\n"
                L"  if CurPageID = wpReady then\r\n"
                L"  begin\r\n"
                L"    Drive := ExtractFileDrive(WizardDirValue());\r\n"
                L"    GetSpaceOnDisk(Drive, True, FreeMB, TotalMB);\r\n"
                L"    if FreeMB < " + reqStr + L" then\r\n"
                L"    begin\r\n"
                L"      MsgBox('Not enough disk space on ' + Drive + '.'#13#10 +\r\n"
                L"             'Required:  " + reqStr + L" MB'#13#10 +\r\n"
                L"             'Available: ' + IntToStr(FreeMB) + ' MB'#13#10#13#10 +\r\n"
                L"             'Please go back and choose a different destination folder.',\r\n"
                L"             mbError, MB_OK);\r\n"
                L"      Result := False;\r\n"
                L"    end;\r\n"
                L"  end;\r\n"
                L"end;\r\n"
                L"\r\n";
        }
        ReplaceAll(tmpl, L"; <<DISK_SPACE_CODE>>", diskCode);
    }

    // ── Replace ; <<DEP_ENFORCE_CODE>> marker ───────────────────────────────
    // Emits Pascal code to enforce inter-component dependencies at runtime.
    // When component A depends on component B:
    //   selecting A auto-selects B; deselecting B auto-deselects A.
    // Produces an empty string (no code) when no components have dependencies.
    {
        std::wstring depCode = BuildDepEnforceCode(comps, IDLG_IsDialogEnabled(IDLG_WELCOME));
        ReplaceAll(tmpl, L"; <<DEP_ENFORCE_CODE>>", depCode);
    }

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

    // ── Replace ; <<TASKS>>, <<ICONS>>, <<RUN>>, <<UNINSTALL_RUN>>, <<CUSTOM_REGISTRY>> ──────
    ReplaceAll(tmpl, L"; <<TASKS>>",
        BuildTasksSection(extra.shortcuts, extra.desktopOptOut,
                          extra.smPinOptOut, extra.tbPinOptOut));
    ReplaceAll(tmpl, L"; <<ICONS>>",
        BuildIconsSection(extra.shortcuts, extra.menuNodes, extra.desktopOptOut));
    ReplaceAll(tmpl, L"; <<RUN>>",
        BuildRunSection(extra.scripts, extra.shortcuts,
                        extra.smPinOptOut, extra.tbPinOptOut));
    ReplaceAll(tmpl, L"; <<UNINSTALL_RUN>>",
        BuildUninstallRunSection(extra.scripts));
    ReplaceAll(tmpl, L"; <<CUSTOM_REGISTRY>>",
        BuildCustomRegistrySection(extra.registryEntries));

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
