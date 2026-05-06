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
#include "db.h"       // ProjectRow
#include "settings.h" // SBuildConfig, InnoLangEntry

// Searches for the inno/ directory relative to the running executable.
// Returns the full path (no trailing backslash) on success, or L"" if not found.
std::wstring ISS_FindInnoDir();

// Generates a complete .iss script and writes it to outPath.
// proj    — current project row (name, version, app_id, app_publisher, directory, …)
// cfg     — build-output settings from SETT_GetBuildConfig()
// langs   — enabled installer languages from SETT_GetInstallerLanguages()
// Returns an empty string on success, or a human-readable error message.
std::wstring ISS_GenerateIss(
    const std::wstring&              templatePath,
    const std::wstring&              outPath,
    const ProjectRow&                proj,
    const SBuildConfig&              cfg,
    const std::vector<InnoLangEntry>& langs);
