#pragma once
/*
 * issgen.h — .iss script generator for SetupCraft.
 *
 * ISS_GenerateIss reads inno/template.iss, substitutes all {#Token} placeholders
 * with per-project values, replaces the "; <<LANGUAGES>>" marker with the
 * correct [Languages] entries derived from SETT_GetInstallerLanguages(), and
 * writes the completed script to outPath.
 *
 * Call ISS_FindInnoDir() first to obtain the directory that contains both
 * template.iss and the local .isl community files (e.g. Swedish.isl).
 * That directory is also the right location for the generated .iss because
 * ISCC resolves relative MessagesFile paths relative to the script file.
 */

#include <windows.h>
#include <string>
#include "db.h"       // ProjectRow, RegistryEntryRow, ScShortcutRow, ScMenuNodeRow, ScriptRow
#include "settings.h" // SBuildConfig, InnoLangEntry

// Searches for the inno/ directory relative to the running executable.
// Returns the full path (no trailing backslash) on success, or L"" if not found.
std::wstring ISS_FindInnoDir();

// Extra data (beyond the core project/settings) consumed by ISS_GenerateIss.
// Mirrors the data that each specialised page provides through its public API.
struct IssExtraData {
    // Shortcuts page
    std::vector<DB::ScShortcutRow> shortcuts;    // desktop + SM + pin entries
    std::vector<DB::ScMenuNodeRow> menuNodes;    // Start Menu folder tree nodes
    bool desktopOptOut  = false;                 // "allow opt-out" desktop checkbox
    bool smPinOptOut    = false;                 // "allow opt-out" SM pin checkbox
    bool tbPinOptOut    = false;                 // "allow opt-out" Taskbar pin checkbox

    // Scripts page
    std::vector<DB::ScriptRow> scripts;          // pre/post/finish/uninstall scripts

    // Custom registry entries from the Registry page
    std::vector<RegistryEntryRow> registryEntries;

    // Files page entries — used to build the [Files] section.
    std::vector<FileRow> files;

    // Wizard images — optional paths to .png/.bmp files for WizardImageFile
    // (large side panel) and WizardSmallImageFile (small top-right image).
    std::wstring wizardImageFile;       // empty = omit (IS6 uses default)
    std::wstring wizardSmallImageFile;  // empty = omit (IS6 uses default)
};

// Generates a complete .iss script and writes it to outPath.
// proj    — current project row (name, version, app_id, app_publisher, directory, …)
// cfg     — build-output settings from SETT_GetBuildConfig()
// langs   — enabled installer languages from SETT_GetInstallerLanguages()
// assocs  — file associations from FA_GetAssociations(); empty vector = none
// types   — install type presets from DB::GetInstallTypesForProject(); empty = no [Types] section
// comps   — components from DB::GetComponentsForProject(); empty = no [Components] section
// extra   — shortcuts, scripts, custom registry entries, dialog flags
// Returns an empty string on success, or a human-readable error message.
std::wstring ISS_GenerateIss(
    const std::wstring&                  templatePath,
    const std::wstring&                  outPath,
    const ProjectRow&                    proj,
    const SBuildConfig&                  cfg,
    const std::vector<InnoLangEntry>&    langs,
    const std::vector<FileAssocRow>&     assocs,
    const std::vector<InstallTypeRow>&   types,
    const std::vector<ComponentRow>&     comps,
    const IssExtraData&                  extra);
