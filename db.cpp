#include "db.h"
#include <windows.h>
#include <objbase.h>   // CoCreateGuid, StringFromGUID2
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <cstdio>

// Generate a new GUID string in the format "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"
static std::wstring GenerateAppId()
{
    GUID g = {};
    CoCreateGuid(&g);
    wchar_t buf[64] = {};
    StringFromGUID2(g, buf, _countof(buf));
    return buf;  // already includes braces, e.g. "{6B29FC40-CA47-1067-B31D-00DD010662DA}"
}

// sqlite3 function pointers
typedef int (*sqlite3_open_v2_t)(const char*, void**, int, const char*);
typedef int (*sqlite3_close_t)(void*);
typedef int (*sqlite3_exec_t)(void*, const char*, int (*)(void*,int,char**,char**), void*, char**);
typedef int (*sqlite3_prepare_v2_t)(void*, const char*, int, void**, const char**);
typedef int (*sqlite3_step_t)(void*);
typedef int (*sqlite3_finalize_t)(void*);
typedef int (*sqlite3_bind_text_t)(void*, int, const char*, int, void(*)(void*));
typedef long long (*sqlite3_last_insert_rowid_t)(void*);
typedef const char* (*sqlite3_errmsg_t)(void*);
typedef const unsigned char* (*sqlite3_column_text_t)(void*, int);
typedef long long (*sqlite3_column_int64_t)(void*, int);

static HMODULE g_sqlite = NULL;
static sqlite3_open_v2_t p_open = NULL;
static sqlite3_close_t p_close = NULL;
static sqlite3_exec_t p_exec = NULL;
static sqlite3_prepare_v2_t p_prepare = NULL;
static sqlite3_step_t p_step = NULL;
static sqlite3_finalize_t p_finalize = NULL;
static sqlite3_bind_text_t p_bind_text = NULL;
static sqlite3_last_insert_rowid_t p_last_insert = NULL;
static sqlite3_errmsg_t p_errmsg = NULL;
static sqlite3_column_text_t p_col_text = NULL;
static sqlite3_column_int64_t p_col_int64 = NULL;

static std::string WToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (sz <= 0) return {};
    std::string out(sz-1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], sz, NULL, NULL);
    return out;
}

static std::wstring Utf8ToW(const std::string &s) {
    if (s.empty()) return {};
    int required = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (required == 0) return {};
    std::wstring out(required, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], required);
    return out;
}

static bool LoadSqlite(const std::wstring &dllPath) {
    if (g_sqlite) return true;
    g_sqlite = LoadLibraryW(dllPath.c_str());
    if (!g_sqlite) return false;
    p_open = (sqlite3_open_v2_t)GetProcAddress(g_sqlite, "sqlite3_open_v2");
    p_close = (sqlite3_close_t)GetProcAddress(g_sqlite, "sqlite3_close");
    p_exec = (sqlite3_exec_t)GetProcAddress(g_sqlite, "sqlite3_exec");
    p_prepare = (sqlite3_prepare_v2_t)GetProcAddress(g_sqlite, "sqlite3_prepare_v2");
    p_step = (sqlite3_step_t)GetProcAddress(g_sqlite, "sqlite3_step");
    p_finalize = (sqlite3_finalize_t)GetProcAddress(g_sqlite, "sqlite3_finalize");
    p_bind_text = (sqlite3_bind_text_t)GetProcAddress(g_sqlite, "sqlite3_bind_text");
    p_last_insert = (sqlite3_last_insert_rowid_t)GetProcAddress(g_sqlite, "sqlite3_last_insert_rowid");
    p_errmsg = (sqlite3_errmsg_t)GetProcAddress(g_sqlite, "sqlite3_errmsg");
    p_col_text = (sqlite3_column_text_t)GetProcAddress(g_sqlite, "sqlite3_column_text");
    p_col_int64 = (sqlite3_column_int64_t)GetProcAddress(g_sqlite, "sqlite3_column_int64");
    return p_open && p_close && p_exec && p_prepare && p_step && p_finalize && p_last_insert && p_errmsg && p_col_text && p_col_int64;
}

static std::wstring GetAppDataDbPath() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, _countof(buf));
    if (len == 0 || len > _countof(buf)) return L"";
    std::wstring dir(buf);
    dir += L"\\SetupCraft";
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryW(dir.c_str(), NULL);
    }
    std::wstring db = dir + L"\\SetupCraft.db";
    return db;
}

bool DB::InitDb() {
    // Try to load sqlite3.dll from exe-directory sqlite3 subfolder
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, _countof(exePath))) return false;
    wchar_t *p = wcsrchr(exePath, L'\\'); if (!p) return false; *p = 0;
    std::wstring dllPath = std::wstring(exePath) + L"\\sqlite3\\sqlite3.dll";
    if (!LoadSqlite(dllPath)) {
        // try mingw_dlls
        dllPath = std::wstring(exePath) + L"\\mingw_dlls\\sqlite3.dll";
        if (!LoadSqlite(dllPath)) return false;
    }

    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);

    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/ | 0x00000008 /*SQLITE_OPEN_URI*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) {
        return false;
    }

    // create tables if not exists
    const char *createProjects = "CREATE TABLE IF NOT EXISTS projects (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, directory TEXT NOT NULL, description TEXT, lang TEXT, version TEXT, created INTEGER, last_updated INTEGER, register_in_windows INTEGER DEFAULT 1, app_icon_path TEXT, app_publisher TEXT, use_components INTEGER DEFAULT 0);";
    const char *createSettings = "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT);";
    const char *createRegistry = "CREATE TABLE IF NOT EXISTS registry_entries (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL, hive TEXT NOT NULL, path TEXT NOT NULL, name TEXT NOT NULL, type TEXT NOT NULL, data TEXT, FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);";
    const char *createFiles = "CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL, source_path TEXT NOT NULL, destination_path TEXT NOT NULL, install_scope TEXT DEFAULT '', FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);";
    const char *createComponents = "CREATE TABLE IF NOT EXISTS components (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL, display_name TEXT NOT NULL DEFAULT '', description TEXT DEFAULT '', notes_rtf TEXT DEFAULT '', is_required INTEGER DEFAULT 0, is_preselected INTEGER DEFAULT 0, source_type TEXT DEFAULT 'folder', source_path TEXT DEFAULT '', dest_path TEXT DEFAULT '', FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);";
    const char *createCompDeps = "CREATE TABLE IF NOT EXISTS component_dependencies (id INTEGER PRIMARY KEY AUTOINCREMENT, component_id INTEGER NOT NULL, depends_on_id INTEGER NOT NULL, UNIQUE(component_id, depends_on_id), FOREIGN KEY(component_id) REFERENCES components(id) ON DELETE CASCADE, FOREIGN KEY(depends_on_id) REFERENCES components(id) ON DELETE CASCADE);";
    char *errmsg = NULL;
    if (p_exec(db, createProjects, NULL, NULL, &errmsg) != 0) {
        // ignore
    }
    if (p_exec(db, createSettings, NULL, NULL, &errmsg) != 0) {
        // ignore
    }
    if (p_exec(db, createRegistry, NULL, NULL, &errmsg) != 0) {
        // ignore
    }
    if (p_exec(db, createFiles, NULL, NULL, &errmsg) != 0) {
        // ignore
    }
    if (p_exec(db, createComponents, NULL, NULL, &errmsg) != 0) {
        // ignore
    }
    if (p_exec(db, createCompDeps, NULL, NULL, &errmsg) != 0) {
        // ignore
    }
    
    // Migrate existing projects table to add new columns if they don't exist
    p_exec(db, "ALTER TABLE projects ADD COLUMN register_in_windows INTEGER DEFAULT 1;", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE projects ADD COLUMN app_icon_path TEXT;", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE projects ADD COLUMN app_publisher TEXT;", NULL, NULL, &errmsg);
    // Add install_scope column to files table to store per-file or per-folder install scope (PerUser/AllUsers/AskAtInstall)
    p_exec(db, "ALTER TABLE files ADD COLUMN install_scope TEXT DEFAULT '';", NULL, NULL, &errmsg);
    // Add inno_flags column to files table for Inno Setup [Files] flags (e.g. ignoreversion, 32bit)
    p_exec(db, "ALTER TABLE files ADD COLUMN inno_flags TEXT DEFAULT '';", NULL, NULL, &errmsg);
    // Add dest_dir_override column to files table for per-file Inno DestDir override (e.g. {sys})
    p_exec(db, "ALTER TABLE files ADD COLUMN dest_dir_override TEXT DEFAULT '';", NULL, NULL, &errmsg);
    // Add use_components column to projects table
    p_exec(db, "ALTER TABLE projects ADD COLUMN use_components INTEGER DEFAULT 0;", NULL, NULL, &errmsg);
    // Add app_id column for Inno AppId GUID (stable across upgrades)
    p_exec(db, "ALTER TABLE projects ADD COLUMN app_id TEXT DEFAULT '';", NULL, NULL, &errmsg);
    // Add flags column to registry_entries for Inno [Registry] per-entry flags (e.g. uninsdeletevalue, 64bit)
    p_exec(db, "ALTER TABLE registry_entries ADD COLUMN flags TEXT DEFAULT '';", NULL, NULL, &errmsg);
    // Add components column to registry_entries for Inno Components: field (per-entry component linkage)
    p_exec(db, "ALTER TABLE registry_entries ADD COLUMN components TEXT DEFAULT '';", NULL, NULL, &errmsg);
    // Ensure component_dependencies table exists for older DBs
    p_exec(db, "CREATE TABLE IF NOT EXISTS component_dependencies (id INTEGER PRIMARY KEY AUTOINCREMENT, component_id INTEGER NOT NULL, depends_on_id INTEGER NOT NULL, UNIQUE(component_id, depends_on_id), FOREIGN KEY(component_id) REFERENCES components(id) ON DELETE CASCADE, FOREIGN KEY(depends_on_id) REFERENCES components(id) ON DELETE CASCADE);", NULL, NULL, &errmsg);
    // Add notes_rtf column to components table for existing databases
    p_exec(db, "ALTER TABLE components ADD COLUMN notes_rtf TEXT DEFAULT '';", NULL, NULL, &errmsg);
    // Add is_preselected column to components table for existing databases
    p_exec(db, "ALTER TABLE components ADD COLUMN is_preselected INTEGER DEFAULT 0;", NULL, NULL, &errmsg);
    // Shortcuts: Start Menu folder nodes and shortcut definitions (Desktop + SM + pins)
    p_exec(db, "CREATE TABLE IF NOT EXISTS sc_menu_nodes (id INTEGER PRIMARY KEY, project_id INTEGER NOT NULL, parent_id INTEGER DEFAULT -1, name TEXT DEFAULT '', FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);", NULL, NULL, &errmsg);
    p_exec(db, "CREATE TABLE IF NOT EXISTS sc_shortcuts (id INTEGER PRIMARY KEY, project_id INTEGER NOT NULL, type INTEGER NOT NULL, sm_node_id INTEGER DEFAULT -1, name TEXT DEFAULT '', exe_path TEXT DEFAULT '', working_dir TEXT DEFAULT '', icon_path TEXT DEFAULT '', icon_index INTEGER DEFAULT 0, run_as_admin INTEGER DEFAULT 0, FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);", NULL, NULL, &errmsg);
    // Add pin columns to sc_shortcuts for existing databases (ALTER TABLE ignores error if already present)
    p_exec(db, "ALTER TABLE sc_shortcuts ADD COLUMN pin_to_start INTEGER DEFAULT 0;", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE sc_shortcuts ADD COLUMN pin_to_taskbar INTEGER DEFAULT 0;", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE sc_shortcuts ADD COLUMN arguments TEXT DEFAULT '';", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE sc_shortcuts ADD COLUMN comment TEXT DEFAULT '';", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE sc_shortcuts ADD COLUMN hotkey TEXT DEFAULT '';", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE external_deps ADD COLUMN download_timeout_sec INTEGER DEFAULT 0;", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE external_deps ADD COLUMN extra_exit_codes TEXT DEFAULT '';", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE external_deps ADD COLUMN max_version TEXT DEFAULT '';", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE external_deps ADD COLUMN required_components TEXT DEFAULT '';", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE external_deps ADD COLUMN detect_version_source INTEGER DEFAULT 0;", NULL, NULL, &errmsg);
    // External dependencies table
    p_exec(db, "CREATE TABLE IF NOT EXISTS external_deps ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "project_id INTEGER NOT NULL, "
        "display_name TEXT DEFAULT '', "
        "is_required INTEGER DEFAULT 1, "
        "delivery INTEGER DEFAULT 0, "
        "install_order INTEGER DEFAULT 0, "
        "detect_reg_key TEXT DEFAULT '', "
        "detect_file_path TEXT DEFAULT '', "
        "min_version TEXT DEFAULT '', "
        "architecture INTEGER DEFAULT 0, "
        "url TEXT DEFAULT '', "
        "silent_args TEXT DEFAULT '', "
        "sha256 TEXT DEFAULT '', "
        "license_path TEXT DEFAULT '', "
        "license_text TEXT DEFAULT '', "
        "credits_text TEXT DEFAULT '', "
        "instructions TEXT DEFAULT '', "
        "offline_behavior INTEGER DEFAULT 0, "
        "FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);",
        NULL, NULL, &errmsg);
    p_exec(db, "CREATE TABLE IF NOT EXISTS dep_instructions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "dep_id INTEGER NOT NULL, "
        "project_id INTEGER NOT NULL, "
        "sort_order INTEGER DEFAULT 0, "
        "rtf_text TEXT DEFAULT '');",
        NULL, NULL, &errmsg);
    p_exec(db, "CREATE TABLE IF NOT EXISTS installer_dialogs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "project_id INTEGER NOT NULL, "
        "dialog_type INTEGER NOT NULL, "
        "content_rtf TEXT DEFAULT '', "
        "UNIQUE(project_id, dialog_type));",
        NULL, NULL, &errmsg);
    p_exec(db, "CREATE TABLE IF NOT EXISTS scripts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "project_id INTEGER NOT NULL, "
        "name TEXT DEFAULT '', "
        "type INTEGER DEFAULT 1, "
        "content TEXT DEFAULT '', "
        "when_to_run INTEGER DEFAULT 1, "
        "run_hidden INTEGER DEFAULT 0, "
        "wait_for_completion INTEGER DEFAULT 1, "
        "description TEXT DEFAULT '', "
        "also_uninstall INTEGER DEFAULT 0, "
        "sort_order INTEGER DEFAULT 0, "
        "FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);",
        NULL, NULL, &errmsg);

    // Default RTF content for each installer dialog type (placeholders: <<AppName>>,
    // <<AppVersion>>, <<AppNameAndVersion>>). INSERT OR IGNORE — never overwrites
    // developer customisations stored in the DB.
    p_exec(db,
        "CREATE TABLE IF NOT EXISTS dialog_defaults ("
        "dialog_type INTEGER PRIMARY KEY, "
        "content_rtf TEXT NOT NULL DEFAULT '');",
        NULL, NULL, &errmsg);
    {
        struct { int type; const wchar_t* rtf; } kDD[] = {
            { 0, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\qc{\b\fs28 <<DlgDefaultWelcomeTitle>>}\par\pard\ql\sb120 <<DlgDefaultWelcomeBody>>\par})" },
            { 1, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}{\colortbl ;\red0\green70\blue140;\red40\green40\blue40;\red100\green100\blue100;\red139\green0\blue0;}\f0\pard\qc\sb0\sa60\cf1{\b\fs28 Public Domain License}\par\pard\qc\sb0\sa200\cf1\fs18 The Unlicense\par\pard\ql\sb0\sa120\cf2\fs20 <<AppName>> is released into the public domain.\par Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.\par In jurisdictions that recognise copyright laws, the author or authors dedicate any and all copyright interest in this software to the public domain. This dedication is intended to be an overt act of relinquishment in perpetuity of all present and future rights to this software under copyright law.\par{\cf4\b THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.}\par\pard\ql\sb120\sa0\cf3\fs18\i <<LicenseCreditNote>>\i0\par})" },
            { 2, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\ql <<DlgDefaultDepsBody>>\par})" },
            { 3, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\ql <<DlgDefaultForMeAllBody>>\par})" },
            { 4, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\ql <<DlgDefaultComponentsBody>>\par})" },
            { 5, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\ql <<DlgDefaultShortcutsBody>>\par})" },
            { 6, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\ql <<DlgDefaultReadyBody1>>\par\pard\ql\sb120 <<DlgDefaultReadyBody2>>\par})" },
            { 7, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\ql <<DlgDefaultInstallingBody>>\par})" },
            { 8, LR"({\rtf1\ansi\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}}\f0\fs20\pard\qc{\b\fs28 <<DlgDefaultFinishTitle>>}\par\pard\ql\sb120 <<DlgDefaultFinishBody1>>\par\pard\ql\sb120 <<DlgDefaultFinishBody2>>\par})" },
        };
        const char* seedSql =
            "INSERT OR IGNORE INTO dialog_defaults (dialog_type, content_rtf) VALUES (?,?);";
        for (int i = 0; i < 9; i++) {
            void* s2 = NULL;
            if (p_prepare(db, seedSql, -1, &s2, NULL) == 0) {
                std::string sT = std::to_string(kDD[i].type);
                std::string sR = WToUtf8(kDD[i].rtf);
                p_bind_text(s2, 1, sT.c_str(), -1, NULL);
                p_bind_text(s2, 2, sR.c_str(),  -1, NULL);
                p_step(s2);
                if (p_finalize) p_finalize(s2);
            }
        }
        // Migration: replace any dialog_defaults rows that pre-date the
        // <<LicenseCreditNote>> i18n placeholder (identified by still containing
        // the old hardcoded English credit text).  Developer-customised defaults
        // that have already adopted the placeholder are left untouched.
        {
            const char* migSql =
                "UPDATE dialog_defaults SET content_rtf=? "
                "WHERE dialog_type=1 "
                "AND content_rtf NOT LIKE '%<<LicenseCreditNote>>%' "
                "AND content_rtf LIKE '%warmly appreciated%';";
            void* s3 = NULL;
            if (p_prepare(db, migSql, -1, &s3, NULL) == 0) {
                std::string sR = WToUtf8(kDD[1].rtf);
                p_bind_text(s3, 1, sR.c_str(), -1, NULL);
                p_step(s3);
                if (p_finalize) p_finalize(s3);
            }
        }
        // Migration: replace dialog_defaults rows for all non-License types that
        // still contain the old hardcoded English body text (identified by NOT yet
        // having any <<DlgDefault…>> placeholder).  Rows that the developer has
        // already customised are left untouched because they will contain their
        // own text, not the new placeholder tokens.
        {
            const char* migSql =
                "UPDATE dialog_defaults SET content_rtf=? "
                "WHERE dialog_type=? "
                "AND content_rtf NOT LIKE '%<<DlgDefault%';";
            for (int i = 0; i < 9; i++) {
                if (i == 1) continue; // License handled separately above
                void* s3 = NULL;
                if (p_prepare(db, migSql, -1, &s3, NULL) == 0) {
                    std::string sR = WToUtf8(kDD[i].rtf);
                    std::string sT = std::to_string(kDD[i].type);
                    p_bind_text(s3, 1, sR.c_str(), -1, NULL);
                    p_bind_text(s3, 2, sT.c_str(), -1, NULL);
                    p_step(s3);
                    if (p_finalize) p_finalize(s3);
                }
            }
        }
    }

    // ── License templates ────────────────────────────────────────────────────
    // Read-only catalogue: developers pick one to pre-fill the License dialog.
    // Columns: id (PK), name (display), spdx_id, img_file (filename in LicenseImg\,
    //          empty = no image), content_rtf (full canonical text, RTF-encoded).
    // Placeholders in content_rtf: <<AppName>>, <<Year>> — substituted at load time.
    p_exec(db,
        "CREATE TABLE IF NOT EXISTS license_templates ("
        "id         INTEGER PRIMARY KEY, "
        "name       TEXT NOT NULL, "
        "spdx_id    TEXT NOT NULL, "
        "img_file   TEXT NOT NULL DEFAULT '', "
        "content_rtf TEXT NOT NULL);",
        NULL, NULL, &errmsg);
    {
        // ── RTF image helper ────────────────────────────────────────────────
        // Reads imgFile from the LicenseImg\ subfolder next to the exe,
        // hex-encodes it, and returns a centred \pict RTF block (PNG or BMP).
        // Returns an empty string if the file cannot be read.
        auto LtBuildPict = [&](const char* imgFile) -> std::string {
            if (!imgFile || imgFile[0] == '\0') return {};
            // Build full path: <exeDir>\LicenseImg\<imgFile>
            wchar_t exeBuf[MAX_PATH];
            if (!GetModuleFileNameW(NULL, exeBuf, _countof(exeBuf))) return {};
            wchar_t* slash = wcsrchr(exeBuf, L'\\');
            if (!slash) return {};
            *slash = 0;
            // Convert imgFile (ASCII) to wide
            int wlen = MultiByteToWideChar(CP_ACP, 0, imgFile, -1, NULL, 0);
            if (wlen <= 0) return {};
            std::wstring wImg(wlen - 1, 0);
            MultiByteToWideChar(CP_ACP, 0, imgFile, -1, &wImg[0], wlen);

            std::wstring isBmp(L".bmp");
            bool isBmpFmt = (wImg.size() >= 4 &&
                             _wcsicmp(wImg.c_str() + wImg.size() - 4, L".bmp") == 0);
            std::wstring fullPath = std::wstring(exeBuf)
                                  + (isBmpFmt ? L"\\LicenseImg\\" : L"\\LicenseImg\\")
                                  + wImg;
            // If not found in LicenseImg\, try root exe dir (GnuLogo.bmp lives there)
            DWORD fa = GetFileAttributesW(fullPath.c_str());
            if (fa == INVALID_FILE_ATTRIBUTES) {
                fullPath = std::wstring(exeBuf) + L"\\" + wImg;
                fa = GetFileAttributesW(fullPath.c_str());
                if (fa == INVALID_FILE_ATTRIBUTES) return {};
            }
            // Read raw bytes
            HANDLE hf = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hf == INVALID_HANDLE_VALUE) return {};
            DWORD sz = GetFileSize(hf, NULL);
            if (sz == 0 || sz == INVALID_FILE_SIZE || sz > 4 * 1024 * 1024) {
                CloseHandle(hf); return {};
            }
            std::string raw(sz, '\0');
            DWORD rd = 0;
            ReadFile(hf, &raw[0], sz, &rd, NULL);
            CloseHandle(hf);
            if (rd != sz) return {};

            // Determine format and image dimensions
            bool isPng = (raw.size() >= 8 &&
                          (unsigned char)raw[0] == 0x89 && raw[1] == 'P' &&
                          raw[2] == 'N' && raw[3] == 'G');
            bool isBmpFile = (raw.size() >= 54 &&
                              (unsigned char)raw[0] == 'B' && raw[1] == 'M');

            int imgW = 0, imgH = 0;
            if (isPng && raw.size() >= 24) {
                // PNG: width at offset 16, height at offset 20 (big-endian)
                imgW = ((unsigned char)raw[16] << 24) | ((unsigned char)raw[17] << 16) |
                       ((unsigned char)raw[18] << 8)  |  (unsigned char)raw[19];
                imgH = ((unsigned char)raw[20] << 24) | ((unsigned char)raw[21] << 16) |
                       ((unsigned char)raw[22] << 8)  |  (unsigned char)raw[23];
            } else if (isBmpFile && raw.size() >= 26) {
                // BMP: width at offset 18, height at offset 22 (little-endian, signed)
                imgW = (int)(((unsigned char)raw[18]) | ((unsigned char)raw[19] << 8) |
                             ((unsigned char)raw[20] << 16) | ((unsigned char)raw[21] << 24));
                imgH = (int)(((unsigned char)raw[22]) | ((unsigned char)raw[23] << 8) |
                             ((unsigned char)raw[24] << 16) | ((unsigned char)raw[25] << 24));
                if (imgH < 0) imgH = -imgH; // negative = top-down
            }
            if (imgW <= 0) imgW = 200;
            if (imgH <= 0) imgH = 100;

            // Scale so display width ≤ 8640 twips (6 inches) and ≤ 5760 twips tall
            int goalW = imgW * 15; // 1 px = 15 twips at 96 DPI
            int goalH = imgH * 15;
            if (goalW > 8640) { goalH = goalH * 8640 / goalW; goalW = 8640; }
            if (goalH > 5760) { goalW = goalW * 5760 / goalH; goalH = 5760; }

            // For BMP files, skip the 14-byte file header (RTF \dibitmap needs DIB)
            const char* pixelData = raw.c_str();
            size_t pixelSize = raw.size();
            if (isBmpFile && raw.size() > 14) {
                pixelData += 14;
                pixelSize -= 14;
            }

            // Hex-encode
            static const char hx[] = "0123456789abcdef";
            std::string hex;
            hex.reserve(pixelSize * 2 + pixelSize / 32 + 4);
            for (size_t i = 0; i < pixelSize; i++) {
                hex += hx[((unsigned char)pixelData[i] >> 4) & 0xF];
                hex += hx[ (unsigned char)pixelData[i]        & 0xF];
                if ((i + 1) % 32 == 0) hex += '\n';
            }

            const char* blip = isPng ? "\\pngblip" : "\\dibitmap0";
            std::string block;
            block.reserve(hex.size() + 200);
            block  = "\\pard\\qc\\sb0\\sa120 {\\pict";
            block += blip;
            block += "\\picw"; block += std::to_string(imgW);
            block += "\\pich"; block += std::to_string(imgH);
            block += "\\picwgoal"; block += std::to_string(goalW);
            block += "\\pichgoal"; block += std::to_string(goalH);
            block += "\r\n";
            block += hex;
            block += "}\\par\r\n";
            return block;
        };

        // ── RTF fragment macros ─────────────────────────────────────────────
        // Colour table: \cf1=blue title, \cf2=dark body, \cf3=grey credit, \cf4=dark-red warning
        #define LT_COLORTBL \
            "{\\colortbl ;\\red0\\green70\\blue140;\\red40\\green40\\blue40;" \
            "\\red100\\green100\\blue100;\\red139\\green0\\blue0;}"
        // Header: open RTF, declare font + colours, set default font
        #define LT_HEAD  "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss\\fcharset0 Arial;}}" LT_COLORTBL "\\f0\\widctlpar"
        // Centred bold title (large)
        #define LT_TITLE(t)  "\\pard\\qc\\sb0\\sa60\\cf1{\\b\\fs28 " t "}\\par"
        // Centred sub-title (version / SPDX line) — extra space below to separate from body
        #define LT_SUB(s)    "\\pard\\qc\\sb0\\sa240\\cf1\\fs18 " s "\\par"
        // Body paragraph format: left-aligned, generous space after each paragraph
        #define LT_BODY      "\\pard\\ql\\sb0\\sa160\\cf2\\fs20 "
        // Sub-headline within the body (e.g. "Preamble", "Terms and Conditions")
        // — bold, blue, extra space before and after
        #define LT_H(h)      "\\pard\\ql\\sb200\\sa80\\cf1{\\b\\fs20 " h "}\\par"
        // Numbered/lettered clause headline (bold, body colour)
        #define LT_CLAUSE(n,t) "\\pard\\ql\\sb160\\sa60\\cf2{\\b\\fs20 " n ". " t "}\\par"
        // Warning / AS-IS disclaimer — dark red, bold
        #define LT_WARN(w)   "\\pard\\ql\\sb120\\sa120\\cf4{\\b\\fs20 " w "}\\par"
        // Credit line at the bottom — grey, italic
        #define LT_CREDIT    "\\pard\\ql\\sb180\\sa0\\cf3\\fs18\\i <<LicenseCreditNote>>\\i0\\par}"

        // Build each template RTF as a std::string so we can prepend the logo image.
        // Struct holds id, display name, SPDX tag, img filename, and RTF body text.
        struct LtEntry {
            int id;
            const char* name;
            const char* spdx;
            const char* img;   // filename in LicenseImg\ (or "" for no image)
            std::string rtf;   // full RTF string built at runtime
        };

        // Helper: wrap static body text into a full RTF document with optional logo image at top.
        // NOTE: body must end with LT_CREDIT which already provides the closing "}" for the RTF group.
        auto LtMake = [&](const char* img, const std::string& body) -> std::string {
            std::string pict = LtBuildPict(img);
            return std::string(LT_HEAD) + pict + body;
        };

        LtEntry kLT[] = {

            // ── 0: The Unlicense ────────────────────────────────────────────
            { 0, "The Unlicense (Public Domain)", "Unlicense", "public-domain-logo-streamlined.png",
              LtMake("public-domain-logo-streamlined.png",
              LT_TITLE("Public Domain License")
              LT_SUB("The Unlicense")
              LT_BODY "Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, "
              "either in source code form or as a compiled binary, for any purpose, commercial or "
              "non-commercial, and by any means.\\par "
              "In jurisdictions that recognise copyright laws, the author or authors dedicate any and all "
              "copyright interest in this software to the public domain. This dedication is intended to be "
              "an overt act of relinquishment in perpetuity of all present and future rights to this "
              "software under copyright law.\\par "
              LT_WARN("THE SOFTWARE IS PROVIDED \\\"AS IS\\\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, "
                      "INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR "
                      "PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, "
                      "DAMAGES OR OTHER LIABILITY ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE "
                      "OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.")
              )
            },

            // ── 1: MIT ──────────────────────────────────────────────────────
            { 1, "MIT License", "MIT", "OpeenSourceInitiative.png",
              LtMake("OpeenSourceInitiative.png",
              LT_TITLE("MIT License")
              LT_SUB("SPDX: MIT")
              LT_BODY "Permission is hereby granted, free of charge, to any person obtaining a copy of this "
              "software and associated documentation files (the \\\"Software\\\"), to deal in the Software "
              "without restriction, including without limitation the rights to use, copy, modify, merge, "
              "publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons "
              "to whom the Software is furnished to do so, subject to the following conditions:\\par "
              "The above copyright notice and this permission notice shall be included in all copies or "
              "substantial portions of the Software.\\par "
              LT_WARN("THE SOFTWARE IS PROVIDED \\\"AS IS\\\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
                      "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR "
                      "A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT "
                      "HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF "
                      "CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE "
                      "OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.")
              )
            },

            // ── 2: Apache 2.0 ───────────────────────────────────────────────
            { 2, "Apache License 2.0", "Apache-2.0", "apache.png",
              LtMake("apache.png",
              LT_TITLE("Apache License")
              LT_SUB("Version 2.0, January 2004  |  SPDX: Apache-2.0")
              LT_BODY "Licensed under the Apache License, Version 2.0 (the \\\"License\\\"); you may not use "
              "this file except in compliance with the License. You may obtain a copy of the License at:\\par "
              "\\pard\\ql\\sb0\\sa160\\cf1\\fs20 https://www.apache.org/licenses/LICENSE-2.0\\par "
              LT_BODY "Unless required by applicable law or agreed to in writing, software distributed "
              "under the License is distributed on an \\\"AS IS\\\" BASIS, WITHOUT WARRANTIES OR "
              "CONDITIONS OF ANY KIND, either express or implied. See the License for the specific "
              "language governing permissions and limitations under the License.\\par "
              LT_H("TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION")
              LT_CLAUSE("1","Definitions")
              LT_BODY "\\\"License\\\" shall mean the terms and conditions for use, reproduction, and "
              "distribution. \\\"Licensor\\\" shall mean the copyright owner or entity authorised by "
              "the copyright owner that is granting the License. \\\"Legal Entity\\\" shall mean the "
              "union of the acting entity and all other entities that control, are controlled by, or "
              "are under common control with that entity. \\\"You\\\" (or \\\"Your\\\") shall mean an "
              "individual or Legal Entity exercising permissions granted by this License. \\\"Source\\\" "
              "form shall mean the preferred form for making modifications, including but not limited "
              "to software source code, documentation source, and configuration files. \\\"Object\\\" "
              "form shall mean any form resulting from mechanical transformation or translation of a "
              "Source form. \\\"Work\\\" shall mean the work of authorship made available under the "
              "License. \\\"Derivative Works\\\" shall mean any work that is based on the Work. "
              "\\\"Contribution\\\" shall mean any work of authorship submitted to the Licensor. "
              "\\\"Contributor\\\" shall mean Licensor and any Legal Entity on behalf of whom a "
              "Contribution has been received by the Licensor.\\par "
              LT_CLAUSE("2","Grant of Copyright License")
              LT_BODY "Subject to the terms and conditions of this License, each Contributor hereby "
              "grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, "
              "irrevocable copyright license to reproduce, prepare Derivative Works of, publicly "
              "display, publicly perform, sublicense, and distribute the Work and such Derivative "
              "Works in Source or Object form.\\par "
              LT_CLAUSE("3","Grant of Patent License")
              LT_BODY "Subject to the terms and conditions of this License, each Contributor hereby "
              "grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, "
              "irrevocable (except as stated in this section) patent license to make, use, sell, "
              "offer for sale, import, and otherwise transfer the Work.\\par "
              LT_CLAUSE("4","Redistribution")
              LT_BODY "You may reproduce and distribute copies of the Work or Derivative Works "
              "thereof in any medium, with or without modifications, and in Source or Object form, "
              "provided that You meet the following conditions: (a) You must give any other "
              "recipients of the Work or Derivative Works a copy of this License; (b) You must "
              "cause any modified files to carry prominent notices stating that You changed the "
              "files; (c) You must retain, in the Source form of any Derivative Works that You "
              "distribute, all copyright, patent, trademark, and attribution notices from the "
              "Source form of the Work; (d) If the Work includes a NOTICE text file, You must "
              "include a readable copy of the attribution notices contained within such NOTICE file.\\par "
              LT_CLAUSE("5","Submission of Contributions")
              LT_BODY "Unless You explicitly state otherwise, any Contribution intentionally "
              "submitted for inclusion in the Work by You to the Licensor shall be under the terms "
              "and conditions of this License, without any additional terms or conditions.\\par "
              LT_CLAUSE("6","Trademarks")
              LT_BODY "This License does not grant permission to use the trade names, trademarks, "
              "service marks, or product names of the Licensor.\\par "
              LT_CLAUSE("7","Disclaimer of Warranty")
              LT_WARN("UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING, LICENSOR PROVIDES "
                      "THE WORK ON AN \\\"AS IS\\\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, "
                      "EITHER EXPRESS OR IMPLIED, INCLUDING, WITHOUT LIMITATION, ANY WARRANTIES OR "
                      "CONDITIONS OF TITLE, NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A "
                      "PARTICULAR PURPOSE. YOU ARE SOLELY RESPONSIBLE FOR DETERMINING THE "
                      "APPROPRIATENESS OF USING OR REDISTRIBUTING THE WORK AND ASSUME ANY RISKS "
                      "ASSOCIATED WITH YOUR EXERCISE OF PERMISSIONS UNDER THIS LICENSE.")
              LT_CLAUSE("8","Limitation of Liability")
              LT_WARN("IN NO EVENT AND UNDER NO LEGAL THEORY, WHETHER IN TORT (INCLUDING NEGLIGENCE), "
                      "CONTRACT, OR OTHERWISE, UNLESS REQUIRED BY APPLICABLE LAW, SHALL ANY CONTRIBUTOR "
                      "BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL, "
                      "OR EXEMPLARY DAMAGES OF ANY CHARACTER ARISING AS A RESULT OF THIS LICENSE OR OUT "
                      "OF THE USE OR INABILITY TO USE THE WORK.")
              )
            },

            // ── 3: GPL v2 ────────────────────────────────────────────────────
            { 3, "GNU General Public License v2.0", "GPL-2.0-only", "GnuLogo.png",
              LtMake("GnuLogo.png",
              LT_TITLE("GNU General Public License")
              LT_SUB("Version 2, June 1991  |  SPDX: GPL-2.0-only")
              LT_BODY "Everyone is permitted to copy and distribute verbatim copies of this license "
              "document, but changing it is not allowed.\\par "
              LT_H("Preamble")
              LT_BODY "The GNU General Public License is a free, copyleft license for software "
              "and other kinds of works. When we speak of free software, we are referring to "
              "freedom, not price. Our General Public Licenses are designed to make sure that "
              "you have the freedom to distribute copies of free software (and charge for them "
              "if you wish), that you receive source code or can get it if you want it, that you "
              "can change the software or use pieces of it in new free programs, and that you "
              "know you can do these things.\\par "
              "To protect your rights, we need to prevent others from denying you these rights "
              "or asking you to surrender the rights. Therefore, you have certain responsibilities "
              "if you distribute copies of the software, or if you modify it: responsibilities to "
              "respect the freedom of others.\\par "
              LT_H("Terms and Conditions for Copying, Distribution and Modification")
              LT_CLAUSE("0","Applicability")
              LT_BODY "This License applies to any program or other work which contains a notice "
              "placed by the copyright holder saying it may be distributed under the terms of this "
              "General Public License. The \\\"Program\\\", below, refers to any such program or "
              "work, and a \\\"work based on the Program\\\" means either the Program or any "
              "derivative work under copyright law.\\par "
              LT_CLAUSE("1","Verbatim Copies")
              LT_BODY "You may copy and distribute verbatim copies of the Program's source code as "
              "you receive it, in any medium, provided that you conspicuously and appropriately "
              "publish on each copy an appropriate copyright notice and disclaimer of warranty; "
              "keep intact all the notices that refer to this License and to the absence of any "
              "warranty; and give any other recipients of the Program a copy of this License "
              "along with the Program.\\par "
              LT_CLAUSE("2","Modified Versions")
              LT_BODY "You may modify your copy or copies of the Program or any portion of it, "
              "thus forming a work based on the Program, and copy and distribute such "
              "modifications or work under the terms of Section 1 above, provided that you also "
              "cause the whole of any work you distribute to be licensed at no charge to all "
              "third parties under the terms of this License.\\par "
              LT_CLAUSE("3","Non-Source Forms")
              LT_BODY "You may copy and distribute the Program (or a work based on it, under "
              "Section 2) in object code or executable form under the terms of Sections 1 and 2 "
              "above provided that you also do one of the following: (a) Accompany it with the "
              "complete corresponding machine-readable source code; (b) Accompany it with a "
              "written offer, valid for at least three years, to give any third party a complete "
              "machine-readable copy of the corresponding source code; or (c) Accompany it with "
              "the information you received as to the offer to distribute corresponding source code.\\par "
              LT_CLAUSE("4","Restrictions")
              LT_BODY "You may not copy, modify, sublicense, or distribute the Program except as "
              "expressly provided under this License. Any attempt otherwise to copy, modify, "
              "sublicense or distribute the Program is void, and will automatically terminate "
              "your rights under this License.\\par "
              LT_CLAUSE("5","Acceptance")
              LT_BODY "You are not required to accept this License, since you have not signed it. "
              "However, nothing else grants you permission to modify or distribute the Program or "
              "its derivative works. By modifying or distributing the Program, you indicate your "
              "acceptance of this License to do so, and all its terms and conditions.\\par "
              LT_CLAUSE("6","Downstream Recipients")
              LT_BODY "Each time you redistribute the Program (or any work based on the Program), "
              "the recipient automatically receives a license from the original licensor to copy, "
              "distribute or modify the Program subject to these terms and conditions.\\par "
              LT_CLAUSE("7","Incompatible Obligations")
              LT_BODY "If conditions are imposed on you that contradict the conditions of this "
              "License, they do not excuse you from the conditions of this License.\\par "
              LT_CLAUSE("8","Geographic Restrictions")
              LT_BODY "If the distribution and/or use of the Program is restricted in certain "
              "countries either by patents or by copyrighted interfaces, the original copyright "
              "holder who places the Program under this License may add an explicit geographical "
              "distribution limitation excluding those countries.\\par "
              LT_CLAUSE("9","Revised Versions of This License")
              LT_BODY "The Free Software Foundation may publish revised and/or new versions of "
              "the General Public License from time to time. Each version is given a "
              "distinguishing version number. If the Program specifies a version number of this "
              "License which applies to it and \\\"any later version\\\", you have the option of "
              "following the terms and conditions either of that version or of any later version "
              "published by the Free Software Foundation.\\par "
              LT_CLAUSE("10","No Warranty Incorporated")
              LT_BODY "If you wish to incorporate parts of the Program into other free programs "
              "whose distribution conditions are different, write to the author to ask for permission.\\par "
              LT_WARN("NO WARRANTY. THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, "
                      "BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF MERCHANTABILITY "
                      "OR FITNESS FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND "
                      "PERFORMANCE OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, "
                      "YOU ASSUME THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.")
              LT_BODY "IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING WILL "
              "ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY AND/OR REDISTRIBUTE THE "
              "PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY GENERAL, "
              "SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OR INABILITY "
              "TO USE THE PROGRAM.\\par "
              )
            },

            // ── 4: GPL v3 ────────────────────────────────────────────────────
            { 4, "GNU General Public License v3.0", "GPL-3.0-only", "GnuLogo.png",
              LtMake("GnuLogo.png",
              LT_TITLE("GNU General Public License")
              LT_SUB("Version 3, 29 June 2007  |  SPDX: GPL-3.0-only")
              LT_BODY "Everyone is permitted to copy and distribute verbatim copies of this license "
              "document, but changing it is not allowed.\\par "
              LT_H("Preamble")
              LT_BODY "The GNU General Public License is a free, copyleft license for software "
              "and other kinds of works. The licenses for most software and other practical works "
              "are designed to take away your freedom to share and change the works. By contrast, "
              "the GNU General Public License is intended to guarantee your freedom to share and "
              "change all versions of a program — to make sure it remains free software for all "
              "its users. When we speak of free software, we are referring to freedom, not price.\\par "
              LT_H("Terms and Conditions")
              LT_CLAUSE("0","Definitions")
              LT_BODY "\\\"This License\\\" refers to version 3 of the GNU General Public License.\\par "
              "\\\"The Program\\\" refers to any copyrightable work licensed under this License.\\par "
              "\\\"Covered work\\\" means either the unmodified Program or a work based on the Program.\\par "
              "\\\"Propagate\\\" a work means to do anything with it that requires permission under "
              "applicable copyright law, other than executing it on a computer or modifying a private copy.\\par "
              "\\\"Convey\\\" a work means any kind of propagation that enables other parties to make "
              "or receive copies.\\par "
              LT_CLAUSE("1","Source Code")
              LT_BODY "The \\\"source code\\\" for a work means the preferred form of the work for "
              "making modifications to it. \\\"Object code\\\" means any non-source form of a work.\\par "
              LT_CLAUSE("2","Basic Permissions")
              LT_BODY "All rights granted under this License are granted for the term of copyright "
              "on the Program, and are irrevocable provided the stated conditions are met. This "
              "License explicitly affirms your unlimited permission to run the unmodified Program.\\par "
              LT_CLAUSE("3","Protecting Users' Legal Rights from Anti-Circumvention Law")
              LT_BODY "No covered work shall be deemed part of an effective technological measure "
              "under any applicable law fulfilling obligations under Article 11 of the WIPO "
              "copyright treaty adopted on 20 December 1996.\\par "
              LT_CLAUSE("4","Conveying Verbatim Copies")
              LT_BODY "You may convey verbatim copies of the Program's source code as you receive "
              "it, in any medium, provided that you conspicuously and appropriately publish on "
              "each copy an appropriate copyright notice; keep intact all notices stating that "
              "this License applies to the code; keep intact all notices of the absence of any "
              "warranty; and give all recipients a copy of this License along with the Program.\\par "
              LT_CLAUSE("5","Conveying Modified Source Versions")
              LT_BODY "You may convey a work based on the Program, or modifications to produce it "
              "from the Program, in the form of source code under the terms of Section 4, provided "
              "that you also meet these conditions: (a) Carry prominent notices stating that you "
              "modified it, and giving a relevant date. (b) Carry prominent notices stating it is "
              "released under this License. (c) License the entire work under this License to "
              "anyone who comes into possession of a copy. (d) If the work has interactive user "
              "interfaces, each must display Appropriate Legal Notices.\\par "
              LT_CLAUSE("6","Conveying Non-Source Forms")
              LT_BODY "You may convey a covered work in object code form under the terms of "
              "Sections 4 and 5, provided that you also convey the machine-readable Corresponding "
              "Source under the terms of this License.\\par "
              LT_CLAUSE("7","Additional Terms")
              LT_BODY "\\\"Additional permissions\\\" are terms that supplement the terms of this "
              "License by making exceptions from one or more of its conditions. Additional "
              "permissions applicable to the entire Program shall be treated as though they were "
              "included in this License, to the extent that they are valid under applicable law.\\par "
              LT_CLAUSE("8","Termination")
              LT_BODY "You may not propagate or modify a covered work except as expressly provided "
              "under this License. Any attempt otherwise to propagate or modify it is void, and "
              "will automatically terminate your rights under this License.\\par "
              LT_CLAUSE("9","Acceptance Not Required for Having Copies")
              LT_BODY "You are not required to accept this License in order to receive or run a "
              "copy of the Program.\\par "
              LT_CLAUSE("10","Automatic Licensing of Downstream Recipients")
              LT_BODY "Each time you convey a covered work, the recipient automatically receives "
              "a license from the original licensors, to run, modify and propagate that work, "
              "subject to this License.\\par "
              LT_CLAUSE("11","Patents")
              LT_BODY "A \\\"contributor\\\" is a copyright holder who authorises use under this "
              "License of the Program or a work on which the Program is based.\\par "
              LT_CLAUSE("12","No Surrender of Others' Freedom")
              LT_BODY "If conditions are imposed on you that contradict the conditions of this "
              "License, they do not excuse you from the conditions of this License.\\par "
              LT_CLAUSE("13","Use with the GNU Affero General Public License")
              LT_BODY "Notwithstanding any other provision of this License, you have permission "
              "to link or combine any covered work with a work licensed under version 3 of the "
              "GNU Affero General Public License into a single combined work, and to convey the "
              "resulting work.\\par "
              LT_CLAUSE("14","Revised Versions of this License")
              LT_BODY "The Free Software Foundation may publish revised and/or new versions of "
              "the GNU General Public License from time to time. Such new versions will be "
              "similar in spirit to the present version, but may differ in detail to address "
              "new problems or concerns.\\par "
              LT_WARN("15. Disclaimer of Warranty. THERE IS NO WARRANTY FOR THE PROGRAM, TO THE "
                      "EXTENT PERMITTED BY APPLICABLE LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING "
                      "THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM \\\"AS IS\\\" "
                      "WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT "
                      "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A "
                      "PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE "
                      "PROGRAM IS WITH YOU.")
              LT_WARN("16. Limitation of Liability. IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW "
                      "OR AGREED TO IN WRITING WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO "
                      "MODIFIES AND/OR CONVEYS THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR "
                      "DAMAGES, INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES "
                      "ARISING OUT OF THE USE OR INABILITY TO USE THE PROGRAM.")
              )
            },

            // ── 5: LGPL v2.1 ─────────────────────────────────────────────────
            { 5, "GNU Lesser General Public License v2.1", "LGPL-2.1-only", "GnuLogo.png",
              LtMake("GnuLogo.png",
              LT_TITLE("GNU Lesser General Public License")
              LT_SUB("Version 2.1, February 1999  |  SPDX: LGPL-2.1-only")
              LT_BODY "Everyone is permitted to copy and distribute verbatim copies of this license "
              "document, but changing it is not allowed.\\par "
              LT_H("Preamble")
              LT_BODY "The licenses for most software are designed to take away your freedom to "
              "share and change it. By contrast, the GNU Lesser General Public License is "
              "intended to permit developers and companies to use and link to the library even "
              "if they are not releasing their own software as free software. When we speak of "
              "free software, we are referring to freedom of use, not price.\\par "
              LT_H("Terms and Conditions for Copying, Distribution and Modification")
              LT_CLAUSE("0","Applicability")
              LT_BODY "This License Agreement applies to any software library or other program "
              "which contains a notice placed by the copyright holder or other authorised party "
              "saying it may be distributed under the terms of this Lesser General Public License "
              "(also called \\\"this License\\\").\\par "
              LT_CLAUSE("1","Verbatim Copies")
              LT_BODY "You may copy and distribute verbatim copies of the Library's complete "
              "source code as you receive it, in any medium, provided that you conspicuously and "
              "appropriately publish on each copy an appropriate copyright notice and disclaimer "
              "of warranty; keep intact all the notices that refer to this License; and distribute "
              "a copy of this License along with the Library.\\par "
              LT_CLAUSE("2","Modified Versions")
              LT_BODY "You may modify your copy or copies of the Library or any portion of it, "
              "thus forming a work based on the Library, and copy and distribute such "
              "modifications under the terms of Section 1, provided that: (a) The modified work "
              "must itself be a software library. (b) You must cause the files modified to carry "
              "prominent notices stating that you changed the files. (c) You must cause the whole "
              "of the work to be licensed at no charge to all third parties under this License.\\par "
              LT_CLAUSE("3","Object Code Incorporation")
              LT_BODY "You may opt to apply the terms of the ordinary GNU General Public License "
              "instead of this License to a given copy of the Library.\\par "
              LT_CLAUSE("4","Combined Libraries")
              LT_BODY "You may place library facilities that are a work based on the Library "
              "side-by-side in a single library together with other library facilities not "
              "covered by this License, and distribute such a combined library, provided that: "
              "(a) You accompany the combined library with a copy of the same work based on the "
              "Library, uncombined; (b) You give prominent notice with the combined library of "
              "the fact that part of it is a work based on the Library.\\par "
              LT_CLAUSE("5","Restrictions on Distribution")
              LT_BODY "You may not copy, modify, sublicense, link with, or distribute the Library "
              "except as expressly provided under this License.\\par "
              LT_CLAUSE("6","Executable Applications Linking to the Library")
              LT_BODY "As an exception to the Sections above, you may also compile or link a "
              "\\\"work that uses the Library\\\" with the Library to produce a work containing "
              "portions of the Library, and distribute that work under terms of your choice, "
              "provided that the terms permit modification of the work for the customer's own "
              "use and reverse engineering for debugging such modifications.\\par "
              LT_WARN("NO WARRANTY. THIS LIBRARY IS PROVIDED \\\"AS IS\\\" WITHOUT WARRANTY OF ANY KIND, "
                      "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
                      "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO "
                      "EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES "
                      "OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, "
                      "ARISING FROM, OUT OF OR IN CONNECTION WITH THE LIBRARY OR THE USE OR OTHER "
                      "DEALINGS IN THE LIBRARY.")
              )
            },

            // ── 6: LGPL v3 ───────────────────────────────────────────────────
            { 6, "GNU Lesser General Public License v3.0", "LGPL-3.0-only", "GnuLogo.png",
              LtMake("GnuLogo.png",
              LT_TITLE("GNU Lesser General Public License")
              LT_SUB("Version 3, 29 June 2007  |  SPDX: LGPL-3.0-only")
              LT_BODY "Everyone is permitted to copy and distribute verbatim copies of this license "
              "document, but changing it is not allowed.\\par "
              LT_BODY "The GNU Lesser General Public License incorporates the terms and conditions "
              "of version 3 of the GNU General Public License, supplemented by the additional "
              "permissions listed below.\\par "
              LT_CLAUSE("0","Additional Definitions")
              LT_BODY "As used herein, \\\"this License\\\" refers to version 3 of the GNU Lesser "
              "General Public License, and the \\\"GNU GPL\\\" refers to version 3 of the GNU "
              "General Public License.\\par "
              "\\\"The Library\\\" means a covered work governed by this License, other than an "
              "Application or a Combined Work. An \\\"Application\\\" is any work that makes use "
              "of an interface provided by the Library, but which is not otherwise based on the "
              "Library. A \\\"Combined Work\\\" is a work produced by combining or linking an "
              "Application with the Library.\\par "
              LT_CLAUSE("1","Exception to Section 3 of the GNU GPL")
              LT_BODY "You may convey a Combined Work under terms of your choice that, taken "
              "together, effectively do not restrict modification of the parts of the Library "
              "contained in the Combined Work and reverse engineering for debugging such "
              "modifications, if you also do each of the following: (a) Give prominent notice "
              "with each copy of the Combined Work that the Library is used in it and that the "
              "Library and its use are covered by this License. (b) Accompany the Combined Work "
              "with a copy of the GNU GPL and this license document. (c) For a Combined Work "
              "that displays copyright notices during execution, include the copyright notice for "
              "the Library among these notices, as well as a reference directing the user to the "
              "copies of the GNU GPL and this license document. (d) Do one of the following: "
              "provide the Minimal Corresponding Source under the terms of this License, or "
              "ensure that the user can produce it.\\par "
              LT_CLAUSE("2","Use of the GNU GPL")
              LT_BODY "You may convey verbatim copies of the Library's complete source code as "
              "you receive it, in any medium, provided that you conspicuously and appropriately "
              "publish on each copy an appropriate copyright notice.\\par "
              LT_CLAUSE("3","Object Code Incorporating Material from Library Header Files")
              LT_BODY "The object code form of an Application may incorporate material from a "
              "header file that is part of the Library. You may convey such object code under "
              "terms of your choice, provided that, if the incorporated material is not limited "
              "to numerical parameters, data structure layouts and accessors, or small macros, "
              "inline functions and templates, you do both of the following: (a) Give prominent "
              "notice with each copy of the object code that the Library is used in it; (b) "
              "Accompany the object code with a copy of the GNU GPL and this license document.\\par "
              LT_CLAUSE("4","Combined Libraries")
              LT_BODY "You may place library facilities that are a work based on the Library "
              "side-by-side in a single library together with other library facilities that are "
              "not Applications and are not covered by this License, and convey such a combined "
              "library under terms of your choice, if you do both of the following: (a) Accompany "
              "the combined library with a copy of the same work based on the Library, uncombined "
              "with any other library facilities, conveyed under the terms of this License; (b) "
              "Give prominent notice with the combined library that part of it is a work based on "
              "the Library, and explaining where to find the accompanying uncombined form.\\par "
              LT_WARN("THERE IS NO WARRANTY FOR THE LIBRARY, TO THE EXTENT PERMITTED BY APPLICABLE "
                      "LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR "
                      "OTHER PARTIES PROVIDE THE LIBRARY \\\"AS IS\\\" WITHOUT WARRANTY OF ANY KIND, "
                      "EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED "
                      "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE "
                      "ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE LIBRARY IS WITH YOU.")
              )
            },

            // ── 7: AGPL v3 ───────────────────────────────────────────────────
            { 7, "GNU Affero General Public License v3.0", "AGPL-3.0-only", "GnuLogo.png",
              LtMake("GnuLogo.png",
              LT_TITLE("GNU Affero General Public License")
              LT_SUB("Version 3, 19 November 2007  |  SPDX: AGPL-3.0-only")
              LT_BODY "Everyone is permitted to copy and distribute verbatim copies of this license "
              "document, but changing it is not allowed.\\par "
              LT_H("Preamble")
              LT_BODY "The GNU Affero General Public License is a free, copyleft license for "
              "software and other kinds of works, specifically designed to ensure cooperation "
              "with the community in the case of network server software. When we speak of free "
              "software, we are referring to freedom, not price. The AGPL is designed to ensure "
              "that, in such cases, the modified source code becomes available to the community. "
              "It requires the operator of a network server to provide the source code of the "
              "modified version running there to the users of that server.\\par "
              LT_H("Terms and Conditions")
              LT_BODY "The AGPL incorporates all Terms and Conditions of the GNU GPL version 3. "
              "The following additional term applies on top of all GPL v3 terms:\\par "
              LT_CLAUSE("13","Remote Network Interaction; Use with the GNU General Public License")
              LT_BODY "Notwithstanding any other provision of this License, if you modify the "
              "Program, your modified version must prominently offer all users interacting with "
              "it remotely through a computer network (if your version supports such interaction) "
              "an opportunity to receive the Corresponding Source of your version by providing "
              "access to the Corresponding Source from a network server at no charge, through "
              "some standard or customary means of facilitating copying of software. This "
              "Corresponding Source shall include the Corresponding Source for any work covered "
              "by version 3 of the GNU General Public License that is incorporated pursuant to "
              "the following paragraph.\\par "
              LT_WARN("THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE "
                      "LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR "
                      "OTHER PARTIES PROVIDE THE PROGRAM \\\"AS IS\\\" WITHOUT WARRANTY OF ANY KIND, "
                      "EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED "
                      "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE "
                      "ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU.")
              )
            },

            // ── 8: BSD 2-Clause ──────────────────────────────────────────────
            { 8, "BSD 2-Clause \"Simplified\" License", "BSD-2-Clause", "BSD.png",
              LtMake("BSD.png",
              LT_TITLE("BSD 2-Clause License")
              LT_SUB("\\\"Simplified\\\"  |  SPDX: BSD-2-Clause")
              LT_BODY "All rights reserved.\\par "
              "Redistribution and use in source and binary forms, with or without modification, "
              "are permitted provided that the following conditions are met:\\par "
              LT_CLAUSE("1","Source Redistribution")
              LT_BODY "Redistributions of source code must retain the above copyright notice, "
              "this list of conditions and the following disclaimer.\\par "
              LT_CLAUSE("2","Binary Redistribution")
              LT_BODY "Redistributions in binary form must reproduce the above copyright notice, "
              "this list of conditions and the following disclaimer in the documentation and/or "
              "other materials provided with the distribution.\\par "
              LT_WARN("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "
                      "\\\"AS IS\\\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT "
                      "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A "
                      "PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER "
                      "OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, "
                      "EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, "
                      "PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; "
                      "OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, "
                      "WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR "
                      "OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF "
                      "ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.")
              )
            },

            // ── 9: BSD 3-Clause ──────────────────────────────────────────────
            { 9, "BSD 3-Clause \"New\" License", "BSD-3-Clause", "BSD.png",
              LtMake("BSD.png",
              LT_TITLE("BSD 3-Clause License")
              LT_SUB("\\\"New\\\" / \\\"Revised\\\"  |  SPDX: BSD-3-Clause")
              LT_BODY "All rights reserved.\\par "
              "Redistribution and use in source and binary forms, with or without modification, "
              "are permitted provided that the following conditions are met:\\par "
              LT_CLAUSE("1","Source Redistribution")
              LT_BODY "Redistributions of source code must retain the above copyright notice, "
              "this list of conditions and the following disclaimer.\\par "
              LT_CLAUSE("2","Binary Redistribution")
              LT_BODY "Redistributions in binary form must reproduce the above copyright notice, "
              "this list of conditions and the following disclaimer in the documentation and/or "
              "other materials provided with the distribution.\\par "
              LT_CLAUSE("3","No Endorsement")
              LT_BODY "Neither the name of the copyright holder nor the names of its contributors "
              "may be used to endorse or promote products derived from this software without "
              "specific prior written permission.\\par "
              LT_WARN("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "
                      "\\\"AS IS\\\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT "
                      "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A "
                      "PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER "
                      "OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, "
                      "EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, "
                      "PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; "
                      "OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, "
                      "WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR "
                      "OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF "
                      "ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.")
              )
            },

            // ── 10: ISC ──────────────────────────────────────────────────────
            { 10, "ISC License", "ISC", "OpeenSourceInitiative.png",
              LtMake("OpeenSourceInitiative.png",
              LT_TITLE("ISC License")
              LT_SUB("SPDX: ISC")
              LT_BODY "Permission to use, copy, modify, and/or distribute this software for any purpose "
              "with or without fee is hereby granted, provided that the above copyright notice "
              "and this permission notice appear in all copies.\\par "
              LT_WARN("THE SOFTWARE IS PROVIDED \\\"AS IS\\\" AND THE AUTHOR DISCLAIMS ALL WARRANTIES "
                      "WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF "
                      "MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY "
                      "SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER "
                      "RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, "
                      "NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE "
                      "USE OR PERFORMANCE OF THIS SOFTWARE.")
              )
            },

            // ── 11: MPL 2.0 ──────────────────────────────────────────────────
            { 11, "Mozilla Public License 2.0", "MPL-2.0", "Mozilla.png",
              LtMake("Mozilla.png",
              LT_TITLE("Mozilla Public License")
              LT_SUB("Version 2.0  |  SPDX: MPL-2.0")
              LT_H("1. Definitions")
              LT_BODY "{\\b 1.1 \\\"Contributor\\\"} — each individual or legal entity that creates, "
              "contributes to the creation of, or owns Covered Software.\\par "
              "{\\b 1.2 \\\"Contributor Version\\\"} — the combination of the Contributions of others "
              "used by a Contributor and that particular Contributor's Contribution.\\par "
              "{\\b 1.3 \\\"Contribution\\\"} — Covered Software of a particular Contributor.\\par "
              "{\\b 1.4 \\\"Covered Software\\\"} — Source Code Form to which the initial Contributor "
              "has attached the notice in Exhibit A, the Executable Form of such Source Code Form, "
              "and Modifications thereof.\\par "
              "{\\b 1.5 \\\"Incompatible With Secondary Licenses\\\"} — either the initial Contributor "
              "has attached the notice described in Exhibit B, or the Covered Software was made "
              "available under the terms of version 1.1 or earlier of the License.\\par "
              "{\\b 1.6 \\\"Executable Form\\\"} — any form of the work other than Source Code Form.\\par "
              "{\\b 1.7 \\\"Larger Work\\\"} — a work that combines Covered Software with other "
              "material in separate files that is not Covered Software.\\par "
              "{\\b 1.8 \\\"License\\\"} — this document.\\par "
              "{\\b 1.9 \\\"Licensable\\\"} — having the right to grant, to the maximum extent "
              "possible, any and all of the rights conveyed by this License.\\par "
              "{\\b 1.10 \\\"Modifications\\\"} — any file in Source Code Form that results from "
              "an addition to, deletion from, or modification of the contents of Covered Software; "
              "or any new file in Source Code Form that contains any Covered Software.\\par "
              "{\\b 1.11 \\\"Patent Claims\\\"} — patent claim(s) licensable by a Contributor that "
              "would be infringed by the making, using, selling or importing of its Contributions "
              "or its Contributor Version.\\par "
              "{\\b 1.12 \\\"Secondary License\\\"} — either the GPL v2.0, LGPL v2.1, AGPL v3.0, "
              "or any later versions of those licenses.\\par "
              "{\\b 1.13 \\\"Source Code Form\\\"} — the form of the work preferred for making modifications.\\par "
              "{\\b 1.14 \\\"You\\\" (or \\\"Your\\\")} — an individual or legal entity exercising "
              "rights under this License.\\par "
              LT_H("2. License Grants and Conditions")
              LT_BODY "{\\b 2.1 Grants.} Each Contributor hereby grants You a world-wide, royalty-free, "
              "non-exclusive license: (a) under intellectual property rights Licensable by such "
              "Contributor to use, reproduce, make available, modify, display, perform, distribute, "
              "and otherwise exploit its Contributions; and (b) under Patent Claims of such "
              "Contributor to make, use, sell, offer for sale, have made, import, and otherwise "
              "transfer either its Contributions or its Contributor Version.\\par "
              "{\\b 2.2 Effective Date.} Licenses become effective for each Contribution on the date "
              "the Contributor first distributes such Contribution.\\par "
              "{\\b 2.3 Limitations on Grant Scope.} The licenses granted in this Section 2 are the "
              "only rights granted under this License. No additional rights or licenses will be "
              "implied from the distribution or licensing of Covered Software under this License.\\par "
              "{\\b 2.4 Subsequent Licenses.} No Contributor makes additional grants as a result of "
              "Your choice to distribute the Covered Software under a subsequent version of this License.\\par "
              "{\\b 2.5 Representation.} Each Contributor represents that the Contributor believes "
              "its Contributions are its original creation(s) or it has sufficient rights to grant "
              "the rights to its Contributions conveyed by this License.\\par "
              LT_H("3. Conditions and Limitations")
              LT_BODY "{\\b 3.1 Distribution of Source Form.} All distribution of Covered Software in "
              "Source Code Form, including any Modifications, must be under the terms of this License.\\par "
              "{\\b 3.2 Distribution of Executable Form.} If You distribute Covered Software in "
              "Executable Form then: (a) such Covered Software must also be made available in Source "
              "Code Form; and (b) You may distribute the Executable Form under the terms of this "
              "License, or sublicense it under different terms, provided the license for the "
              "Executable Form does not attempt to limit the recipients' rights in the Source Code Form.\\par "
              "{\\b 3.3 Distribution of a Larger Work.} You may create and distribute a Larger Work "
              "under terms of Your choice, provided that You also comply with the requirements of "
              "this License for the Covered Software.\\par "
              LT_H("4. Inability to Comply Due to Statute or Regulation")
              LT_BODY "If it is impossible for You to comply with any of the terms of this License "
              "with respect to some or all of the Covered Software due to statute, judicial order, "
              "or regulation then You must: (a) comply with the terms of this License to the maximum "
              "extent possible; and (b) describe the limitations and the code they affect.\\par "
              LT_H("5. Termination")
              LT_BODY "5.1 The rights granted under this License will terminate automatically if You "
              "fail to comply with any of its terms. However, if You become compliant, then the "
              "rights granted under this License from a particular Contributor are reinstated.\\par "
              "5.2 If You initiate litigation against any entity by asserting a patent infringement "
              "claim alleging that a Contributor Version directly or indirectly infringes any "
              "patent, then the rights granted to You by any and all Contributors for the Covered "
              "Software under Section 2.1 shall terminate.\\par "
              LT_H("6. Disclaimer of Warranty")
              LT_WARN("COVERED SOFTWARE IS PROVIDED UNDER THIS LICENSE ON AN \\\"AS IS\\\" BASIS, WITHOUT "
                      "WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, "
                      "WARRANTIES THAT THE COVERED SOFTWARE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A "
                      "PARTICULAR PURPOSE OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND "
                      "PERFORMANCE OF THE COVERED SOFTWARE IS WITH YOU.")
              LT_H("7. Limitation of Liability")
              LT_WARN("TO THE EXTENT PERMITTED BY APPLICABLE LAW, IN NO EVENT WILL ANY CONTRIBUTOR BE "
                      "LIABLE TO YOU ON ANY LEGAL THEORY (INCLUDING, WITHOUT LIMITATION, NEGLIGENCE) OR "
                      "OTHERWISE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES "
                      "OF ANY CHARACTER INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOST PROFITS, LOSS OF "
                      "GOODWILL, WORK STOPPAGE, COMPUTER FAILURE OR MALFUNCTION, OR ANY AND ALL OTHER "
                      "COMMERCIAL DAMAGES OR LOSSES, EVEN IF SUCH CONTRIBUTOR HAS BEEN ADVISED OF THE "
                      "POSSIBILITY OF SUCH DAMAGES.")
              )
            },

            // ── 12: Boost Software License ───────────────────────────────────
            { 12, "Boost Software License 1.0", "BSL-1.0", "BSL-1.0 (Boost).png",
              LtMake("BSL-1.0 (Boost).png",
              LT_TITLE("Boost Software License")
              LT_SUB("Version 1.0  |  SPDX: BSL-1.0")
              LT_BODY "Boost Software License — Version 1.0 — August 17th, 2003\\par "
              "Permission is hereby granted, free of charge, to any person or organisation "
              "obtaining a copy of the software and accompanying documentation covered by this "
              "license (the \\\"Software\\\") to use, reproduce, display, distribute, execute, "
              "and transmit the Software, and to prepare derivative works of the Software, and "
              "to permit third-parties to whom the Software is furnished to do so, all subject "
              "to the following:\\par "
              "The copyright notices in the Software and this entire statement, including the "
              "above license grant, this restriction and the following disclaimer, must be "
              "included in all copies of the Software, in whole or in part, and all derivative "
              "works of the Software, unless such copies or derivative works are solely in the "
              "form of machine-executable object code generated by a source language processor.\\par "
              LT_WARN("THE SOFTWARE IS PROVIDED \\\"AS IS\\\", WITHOUT WARRANTY OF ANY KIND, EXPRESS "
                      "OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
                      "FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT "
                      "SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE FOR "
                      "ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING "
                      "FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS "
                      "IN THE SOFTWARE.")
              )
            },

            // ── 13: EUPL 1.2 ─────────────────────────────────────────────────
            { 13, "European Union Public Licence 1.2", "EUPL-1.2", "Logo_EUPL.svg.png",
              LtMake("Logo_EUPL.svg.png",
              LT_TITLE("European Union Public Licence")
              LT_SUB("Version 1.2  |  SPDX: EUPL-1.2")
              LT_BODY "\\u169? The European Union 2007, 2016\\par "
              "This European Union Public Licence (the \\\"EUPL\\\") applies to the Work as "
              "defined below, which is provided under the terms of this Licence. Any use of "
              "the Work, other than as authorised under this Licence is prohibited.\\par "
              "The Work is provided under the terms of this Licence when the Licensor has placed "
              "the following notice immediately following the copyright notice for the Work:\\par "
              "\\pard\\qc\\sb80\\sa80\\cf1\\fs18 Licensed under the EUPL\\par "
              LT_H("1. Definitions")
              LT_BODY "\\\"The Licence\\\": the present Licence. "
              "\\\"The Original Work\\\": the work or software distributed by the Licensor under "
              "this Licence, available as Source Code and also as Executable Code. "
              "\\\"Derivative Works\\\": works that could be created by the Licensee, based upon "
              "the Original Work or modifications thereof. "
              "\\\"The Work\\\": the Original Work or its Derivative Works. "
              "\\\"The Source Code\\\": the human-readable form of the Work which is the most "
              "convenient for people to study and modify. "
              "\\\"The Executable Code\\\": any code which has generally been compiled and which "
              "is meant to be interpreted by a computer as a program. "
              "\\\"The Licensor\\\": the natural or legal person(s) that distributes or "
              "communicates the Work under the Licence. "
              "\\\"Contributor(s)\\\": any natural or legal person who modifies the Work under "
              "the Licence, or otherwise contributes to the creation of a Derivative Work. "
              "\\\"The Licensee\\\" or \\\"You\\\": any natural or legal person who makes any "
              "usage of the Work under the terms of the Licence.\\par "
              LT_H("2. Scope of the Rights Granted by the Licence")
              LT_BODY "The Licensor hereby grants You a world-wide, royalty-free, non-exclusive, "
              "sublicensable licence to do the following, for the duration of copyright vested "
              "in the Original Work: use the Work in any circumstance; reproduce the Work; "
              "modify the Original Work and make Derivative Works based upon the Work; "
              "communicate and distribute the Work or copies thereof; lend and rent the Work; "
              "sublicense rights in the Work.\\par "
              LT_H("3. Communication of the Source Code")
              LT_BODY "The Licensor may provide the Work either in Source Code form, or as "
              "Executable Code. If the Work is provided as Executable Code, the Licensor "
              "provides in addition a machine-readable copy of the Source Code of the Work "
              "along with each copy of the Work that the Licensor distributes.\\par "
              LT_H("4. Limitations on Copyright")
              LT_BODY "Nothing in this Licence is intended to deprive the Licensee of the "
              "benefits from any exception or limitation to the exclusive rights of the rights "
              "owners in the Original Work or software.\\par "
              LT_H("5. Obligations of the Licensee")
              LT_BODY "The grant of the rights mentioned above is subject to restrictions and "
              "obligations imposed on the Licensee: Attribution — the Licensee shall keep intact "
              "all copyright, patent or trademark notices and all notices that refer to the "
              "Licence and to the disclaimer of warranties. The Licensee must include a copy of "
              "such notices and a copy of the Licence with every copy of the Work it distributes.\\par "
              LT_H("6. Chain of Authorship")
              LT_BODY "The original Licensor warrants that the copyright in the Original Work "
              "granted hereunder is owned by him/her or licensed to him/her and that he/she has "
              "the power and authority to grant the Licence.\\par "
              LT_H("7. Disclaimer of Warranty")
              LT_WARN("The Work is provided under the Licence on an \\\"as is\\\" basis and without "
                      "warranties of any kind concerning the Work, including without limitation "
                      "merchantability, fitness for a particular purpose, absence of defects or errors, "
                      "accuracy, non-infringement of intellectual property rights other than copyright.")
              LT_H("8. Disclaimer of Liability")
              LT_WARN("Except in the cases of wilful misconduct or damages directly caused to natural "
                      "persons, the Licensor will in no event be liable for any direct or indirect, "
                      "material or moral, damages of any kind, arising out of the Licence or of the "
                      "use of the Work, including without limitation, damages for loss of goodwill, "
                      "work stoppage, computer failure or malfunction, loss of data or any commercial damage.")
              )
            },

            // ── 14: CC0 1.0 ──────────────────────────────────────────────────
            { 14, "Creative Commons Zero v1.0 Universal", "CC0-1.0", "CC0-1.0.png",
              LtMake("CC0-1.0.png",
              LT_TITLE("Creative Commons Zero")
              LT_SUB("v1.0 Universal  |  SPDX: CC0-1.0")
              LT_BODY "The person who associated a work with this deed has dedicated the work to the "
              "public domain by waiving all of their rights to the work worldwide under copyright "
              "law, including all related and neighbouring rights, to the extent allowed by law. "
              "You can copy, modify, distribute and perform the work, even for commercial "
              "purposes, all without asking permission.\\par "
              LT_H("No Copyright")
              LT_BODY "The person who associated a work with this deed has dedicated the work to "
              "the public domain by waiving all of their rights to the work worldwide under "
              "copyright law, including all related and neighbouring rights, to the extent "
              "allowed by law.\\par "
              LT_H("Other Information")
              LT_BODY "In no way are the patent or trademark rights of any person affected by "
              "CC0, nor are the rights that other persons may have in the work or in how the "
              "work is used, such as publicity or privacy rights.\\par "
              "Unless expressly stated otherwise, the person who associated a work with this "
              "deed makes no warranties about the work, and disclaims liability for all uses "
              "of the work, to the fullest extent permitted by applicable law.\\par "
              "When using or citing the work, you should not imply endorsement by the author "
              "or the affirmer.\\par "
              )
            },

            // ── 15: CC-BY 4.0 ─────────────────────────────────────────────────
            { 15, "Creative Commons Attribution 4.0", "CC-BY-4.0", "CC-BY-4.0.png",
              LtMake("CC-BY-4.0.png",
              LT_TITLE("Creative Commons Attribution 4.0")
              LT_SUB("International  |  SPDX: CC-BY-4.0")
              LT_BODY "This work is licensed under the Creative Commons Attribution 4.0 International "
              "License. To view a copy of this license, visit "
              "http://creativecommons.org/licenses/by/4.0/ or send a letter to Creative Commons, "
              "PO Box 1866, Mountain View, CA 94042, USA.\\par "
              LT_H("You are free to:")
              LT_BODY "Share — copy and redistribute the material in any medium or format.\\par "
              "Adapt — remix, transform, and build upon the material for any purpose, even "
              "commercially.\\par "
              "The licensor cannot revoke these freedoms as long as you follow the licence terms.\\par "
              LT_H("Under the following terms:")
              LT_BODY "{\\b Attribution} — You must give appropriate credit, provide a link to "
              "the license, and indicate if changes were made. You may do so in any reasonable "
              "manner, but not in any way that suggests the licensor endorses you or your use.\\par "
              "{\\b No additional restrictions} — You may not apply legal terms or technological "
              "measures that legally restrict others from doing anything the license permits.\\par "
              LT_H("Notices:")
              LT_BODY "You do not have to comply with the license for elements of the material "
              "in the public domain or where your use is permitted by an applicable exception "
              "or limitation. No warranties are given. The license may not give you all of the "
              "permissions necessary for your intended use. For example, other rights such as "
              "publicity, privacy, or moral rights may limit how you use the material.\\par "
              )
            },

            // ── 16: CC-BY-SA 4.0 ──────────────────────────────────────────────
            { 16, "Creative Commons Attribution-ShareAlike 4.0", "CC-BY-SA-4.0", "CC-BY-SA-4.0.png",
              LtMake("CC-BY-SA-4.0.png",
              LT_TITLE("Creative Commons Attribution-ShareAlike 4.0")
              LT_SUB("International  |  SPDX: CC-BY-SA-4.0")
              LT_BODY "This work is licensed under the Creative Commons Attribution-ShareAlike 4.0 "
              "International License. To view a copy of this license, visit "
              "http://creativecommons.org/licenses/by-sa/4.0/ or send a letter to Creative "
              "Commons, PO Box 1866, Mountain View, CA 94042, USA.\\par "
              LT_H("You are free to:")
              LT_BODY "Share — copy and redistribute the material in any medium or format.\\par "
              "Adapt — remix, transform, and build upon the material for any purpose, even "
              "commercially.\\par "
              "The licensor cannot revoke these freedoms as long as you follow the licence terms.\\par "
              LT_H("Under the following terms:")
              LT_BODY "{\\b Attribution} — You must give appropriate credit, provide a link to "
              "the license, and indicate if changes were made. You may do so in any reasonable "
              "manner, but not in any way that suggests the licensor endorses you or your use.\\par "
              "{\\b ShareAlike} — If you remix, transform, or build upon the material, you must "
              "distribute your contributions under the same license as the original.\\par "
              "{\\b No additional restrictions} — You may not apply legal terms or technological "
              "measures that legally restrict others from doing anything the license permits.\\par "
              LT_H("Notices:")
              LT_BODY "You do not have to comply with the license for elements of the material "
              "in the public domain or where your use is permitted by an applicable exception "
              "or limitation. No warranties are given.\\par "
              )
            },

            // ── 17: Artistic 2.0 ──────────────────────────────────────────────
            { 17, "Artistic License 2.0", "Artistic-2.0", "OpeenSourceInitiative.png",
              LtMake("OpeenSourceInitiative.png",
              LT_TITLE("Artistic License")
              LT_SUB("Version 2.0  |  SPDX: Artistic-2.0")
              LT_BODY "The intent of this document is to state the conditions under which a Package "
              "may be copied, such that the Copyright Holder maintains some semblance of "
              "artistic control over the development of the package, while giving the users of "
              "the package the right to use and distribute the Package in a more-or-less "
              "customary fashion, plus the right to make reasonable modifications.\\par "
              LT_H("Definitions")
              LT_BODY "\\\"The Package\\\" — the collection of files distributed by the Copyright "
              "Holder, and derivatives of that collection and/or of those files.\\par "
              "\\\"Standard Version\\\" — such a Package if it has not been modified, or has been "
              "modified only in ways explicitly requested by the Copyright Holder.\\par "
              "\\\"Copyright Holder\\\" — whoever is named in the copyright or copyrights for the package.\\par "
              "\\\"You\\\" — you, if you are thinking about copying or distributing this Package.\\par "
              "\\\"Freely Available\\\" — no fee is charged for the item itself, though there may "
              "be fees involved in handling the item.\\par "
              LT_CLAUSE("1","Source Copies")
              LT_BODY "You may make and give away verbatim copies of the source form of the "
              "Standard Version of this Package without restriction, provided that you duplicate "
              "all of the original copyright notices and associated disclaimers.\\par "
              LT_CLAUSE("2","Bug-Fix Modifications")
              LT_BODY "You may apply bug fixes, portability fixes and other modifications derived "
              "from the Public Domain or from the Copyright Holder. A Package modified in such "
              "a way shall still be considered the Standard Version.\\par "
              LT_CLAUSE("3","Distribution of Modified Versions")
              LT_BODY "You may otherwise modify your copy of this Package in any way, provided "
              "that you insert a prominent notice in each changed file stating how and when you "
              "changed that file, and provided that you do at least ONE of the following: "
              "(a) place your modifications in the Public Domain or otherwise make them Freely "
              "Available; (b) use the modified Package only within your corporation or "
              "organisation; (c) rename any non-standard executables so names do not conflict "
              "with standard executables; or (d) make other distribution arrangements with "
              "the Copyright Holder.\\par "
              LT_CLAUSE("4","Aggregated Packages")
              LT_BODY "You may distribute the programs of this Package in object code or "
              "executable form, provided that you do at least ONE of the following: "
              "(a) distribute a Standard Version of the executables and library files; "
              "(b) accompany the distribution with the machine-readable source; (c) give "
              "non-standard executables non-standard names, and clearly document the "
              "differences; or (d) make other distribution arrangements with the Copyright Holder.\\par "
              LT_CLAUSE("5","Charging for Copies")
              LT_BODY "You may charge a reasonable copying fee for any distribution of this "
              "Package. You may charge any fee you choose for support of this Package. You may "
              "not charge a fee for this Package itself. However, you may distribute this "
              "Package in aggregate with other (possibly commercial) programs as part of a "
              "larger (possibly commercial) software distribution provided that you do not "
              "advertise this Package as a product of your own.\\par "
              LT_CLAUSE("6","C Subroutines")
              LT_BODY "The scripts and library files supplied as input to or produced as output "
              "from the programs of this Package do not automatically fall under the copyright "
              "of this Package, but belong to whomever generated them, and may be sold "
              "commercially, and may be aggregated with this Package.\\par "
              LT_CLAUSE("7","Aggregation with Non-Free Software")
              LT_BODY "C or perl subroutines supplied by you and linked into this Package shall "
              "not be considered part of this Package.\\par "
              LT_CLAUSE("8","No Endorsement")
              LT_BODY "The name of the Copyright Holder may not be used to endorse or promote "
              "products derived from this software without specific prior written permission.\\par "
              LT_WARN("THIS PACKAGE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "
                      "\\\"AS IS\\\" AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, "
                      "WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS "
                      "FOR A PARTICULAR PURPOSE.")
              )
            },

            // ── 18: WTFPL ─────────────────────────────────────────────────────
            { 18, "Do What The F*ck You Want To Public License", "WTFPL", "wtfpl-badge-1.png",
              LtMake("wtfpl-badge-1.png",
              LT_TITLE("Do What The F*ck You Want To")
              LT_SUB("Public License — Version 2  |  SPDX: WTFPL")
              LT_BODY "Everyone is permitted to copy and distribute verbatim or modified copies of this "
              "license document, and changing it is allowed as long as the name is changed.\\par "
              LT_H("DO WHAT THE F*CK YOU WANT TO PUBLIC LICENSE")
              LT_BODY "TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION\\par "
              LT_CLAUSE("0","The Whole Point")
              LT_BODY "You just DO WHAT THE F*CK YOU WANT TO.\\par "
              )
            },
        };
        #undef LT_COLORTBL
        #undef LT_HEAD
        #undef LT_TITLE
        #undef LT_SUB
        #undef LT_BODY
        #undef LT_H
        #undef LT_CLAUSE
        #undef LT_WARN
        #undef LT_CREDIT

        // Seed new rows and UPDATE all existing rows so images + text refresh on every startup.
        // Uses INSERT ... ON CONFLICT ... DO UPDATE (SQLite UPSERT, supported since v3.24.0).
        const int kLTCount = (int)(sizeof(kLT) / sizeof(kLT[0]));
        const char* upsertLTSql =
            "INSERT INTO license_templates (id, name, spdx_id, img_file, content_rtf) "
            "VALUES (?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET "
            "  name=excluded.name, spdx_id=excluded.spdx_id, "
            "  img_file=excluded.img_file, content_rtf=excluded.content_rtf;";
        for (int i = 0; i < kLTCount; i++) {
            void* s2 = NULL;
            if (p_prepare(db, upsertLTSql, -1, &s2, NULL) == 0) {
                std::string sId = std::to_string(kLT[i].id);
                p_bind_text(s2, 1, sId.c_str(),        -1, NULL);
                p_bind_text(s2, 2, kLT[i].name,        -1, NULL);
                p_bind_text(s2, 3, kLT[i].spdx,        -1, NULL);
                p_bind_text(s2, 4, kLT[i].img,         -1, NULL);
                p_bind_text(s2, 5, kLT[i].rtf.c_str(), -1, NULL);
                p_step(s2);
                if (p_finalize) p_finalize(s2);
            }
        }
    }

    // Check if projects table is empty, if so add SetupCraft project
    const char *countSql = "SELECT COUNT(*) FROM projects;";
    void *stmt = NULL;
    if (p_prepare(db, countSql, -1, &stmt, NULL) == 0) {
        if (p_step(stmt) == 100 /*SQLITE_ROW*/) {
            long long count = p_col_int64(stmt, 0);
            if (count == 0) {
                // Insert SetupCraft project
                if (p_finalize) p_finalize(stmt);
                
                // Get current exe directory
                wchar_t exePath[MAX_PATH];
                if (GetModuleFileNameW(NULL, exePath, _countof(exePath))) {
                    wchar_t *p2 = wcsrchr(exePath, L'\\');
                    if (p2) {
                        *p2 = 0;
                        std::wstring projectDir(exePath);
                        
                        const char *insertSql = "INSERT INTO projects (name, directory, description, lang, version, created, last_updated) VALUES (?, ?, ?, ?, ?, ?, ?);";
                        void *insertStmt = NULL;
                        if (p_prepare(db, insertSql, -1, &insertStmt, NULL) == 0) {
                            time_t now = time(NULL);
                            std::string sNow;
                            { std::ostringstream os; os << (long long)now; sNow = os.str(); }
                            
                            // Generate version string YYYY.MM.DD.HH
                            struct tm *timeinfo = localtime(&now);
                            char versionBuf[32];
                            sprintf(versionBuf, "%04d.%02d.%02d.%02d", 
                                timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, 
                                timeinfo->tm_mday, timeinfo->tm_hour);
                            
                            const char *name = "SetupCraft";
                            std::string dir = WToUtf8(projectDir);
                            const char *desc = "Setup project installer creator";
                            const char *lang = "no_NB";
                            
                            p_bind_text(insertStmt, 1, name, -1, NULL);
                            p_bind_text(insertStmt, 2, dir.c_str(), -1, NULL);
                            p_bind_text(insertStmt, 3, desc, -1, NULL);
                            p_bind_text(insertStmt, 4, lang, -1, NULL);
                            p_bind_text(insertStmt, 5, versionBuf, -1, NULL);
                            p_bind_text(insertStmt, 6, sNow.c_str(), -1, NULL);
                            p_bind_text(insertStmt, 7, sNow.c_str(), -1, NULL);
                            
                            p_step(insertStmt);
                            if (p_finalize) p_finalize(insertStmt);
                        }
                    }
                }
            } else {
                if (p_finalize) p_finalize(stmt);
            }
        } else {
            if (p_finalize) p_finalize(stmt);
        }
    }
    
    p_close(db);
    return true;
}

bool DB::InsertProject(const std::wstring &name, const std::wstring &directory, const std::wstring &description, const std::wstring &lang, const std::wstring &version, int &outId) {
    outId = -1;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "INSERT INTO projects (name, directory, description, lang, version, created, last_updated, app_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";  // app_id: stable GUID for Inno AppId
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string n = WToUtf8(name);
    std::string d = WToUtf8(directory);
    std::string desc = WToUtf8(description);
    std::string l = WToUtf8(lang);
    std::string v = WToUtf8(version);
    time_t now = time(NULL);
    if (p_bind_text) p_bind_text(stmt, 1, n.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, d.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3, desc.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4, l.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 5, v.c_str(), -1, NULL);
    // bind created and last_updated as text of epoch
    std::ostringstream os; os << (long long)now;
    std::string sEpoch = os.str();
    std::string sAppId = WToUtf8(GenerateAppId());
    if (p_bind_text) p_bind_text(stmt, 6, sEpoch.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 7, sEpoch.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 8, sAppId.c_str(),  -1, NULL);

    int rc = p_step(stmt);
    (void)rc;
    if (p_finalize) p_finalize(stmt);
    if (p_last_insert) {
        long long id = p_last_insert(db);
        outId = (int)id;
    }
    p_close(db);
    return true;
}

bool DB::DeleteFilesForProject(int projectId) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "DELETE FROM files WHERE project_id = ?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    if (p_bind_text) {
        std::ostringstream os; os << projectId; std::string sId = os.str();
        p_bind_text(stmt, 1, sId.c_str(), -1, NULL);
    }
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

bool DB::InsertFile(int projectId, const std::wstring &sourcePath, const std::wstring &destPath, const std::wstring &installScope, const std::wstring &innoFlags, const std::wstring &destDirOverride) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "INSERT INTO files (project_id, source_path, destination_path, install_scope, inno_flags, dest_dir_override) VALUES (?, ?, ?, ?, ?, ?);";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }

    std::string sProject = std::to_string(projectId);
    std::string sSource = WToUtf8(sourcePath);
    std::string sDest = WToUtf8(destPath);
    std::string sScope = WToUtf8(installScope);
    std::string sFlags = WToUtf8(innoFlags);
    std::string sDestDir = WToUtf8(destDirOverride);

    if (p_bind_text) p_bind_text(stmt, 1, sProject.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, sSource.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3, sDest.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4, sScope.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 5, sFlags.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 6, sDestDir.c_str(), -1, NULL);

    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

std::vector<FileRow> DB::GetFilesForProject(int projectId) {
    std::vector<FileRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;

    const char *sql = "SELECT id, source_path, destination_path, install_scope, inno_flags, dest_dir_override FROM files WHERE project_id = ? ORDER BY id ASC;";;
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sId = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sId.c_str(), -1, NULL);

    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        FileRow r;
        r.id         = (int)p_col_int64(stmt, 0);
        r.project_id = projectId;
        const unsigned char *c1 = p_col_text(stmt, 1);
        const unsigned char *c2 = p_col_text(stmt, 2);
        const unsigned char *c3 = p_col_text(stmt, 3);
        r.source_path      = Utf8ToW(c1 ? (const char*)c1 : "");
        r.destination_path = Utf8ToW(c2 ? (const char*)c2 : "");
        r.install_scope    = Utf8ToW(c3 ? (const char*)c3 : "");
        const unsigned char *c4 = p_col_text(stmt, 4);
        r.inno_flags       = Utf8ToW(c4 ? (const char*)c4 : "");
        const unsigned char *c5 = p_col_text(stmt, 5);
        r.dest_dir_override = Utf8ToW(c5 ? (const char*)c5 : "");
        out.push_back(r);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

std::vector<ProjectRow> DB::ListProjects() {
    std::vector<ProjectRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;

    const char *sql = "SELECT id, name, directory, description, lang, version, created, last_updated, register_in_windows, app_icon_path, app_publisher, use_components, app_id FROM projects ORDER BY last_updated DESC;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        ProjectRow r;
        r.id = (int)p_col_int64(stmt, 0);
        const unsigned char *t0 = p_col_text(stmt, 1);
        const unsigned char *t1 = p_col_text(stmt, 2);
        const unsigned char *t2 = p_col_text(stmt, 3);
        const unsigned char *t3 = p_col_text(stmt, 4);
        const unsigned char *t4 = p_col_text(stmt, 5);
        const unsigned char *t5 = p_col_text(stmt, 6);
        const unsigned char *t6 = p_col_text(stmt, 7);
        const unsigned char *t7 = p_col_text(stmt, 9);
        const unsigned char *t8 = p_col_text(stmt, 10);
        r.name = Utf8ToW(t0 ? (const char*)t0 : "");
        r.directory = Utf8ToW(t1 ? (const char*)t1 : "");
        r.description = Utf8ToW(t2 ? (const char*)t2 : "");
        r.lang = Utf8ToW(t3 ? (const char*)t3 : "");
        r.version = Utf8ToW(t4 ? (const char*)t4 : "");
        r.created = t5 ? atoll((const char*)t5) : 0;
        r.last_updated = t6 ? atoll((const char*)t6) : 0;
        r.register_in_windows = (int)p_col_int64(stmt, 8);
        r.app_icon_path = Utf8ToW(t7 ? (const char*)t7 : "");
        r.app_publisher = Utf8ToW(t8 ? (const char*)t8 : "");
        r.use_components = (int)p_col_int64(stmt, 11);
        const unsigned char *t9 = p_col_text(stmt, 12);
        r.app_id = Utf8ToW(t9 ? (const char*)t9 : "");
        // Existing project with no GUID yet — generate one and persist it.
        if (r.app_id.empty() && r.id > 0) {
            r.app_id = GenerateAppId();
            const char* updSql = "UPDATE projects SET app_id=? WHERE id=?;";
            void* us = NULL;
            if (p_prepare(db, updSql, -1, &us, NULL) == 0) {
                std::string sAid = WToUtf8(r.app_id), sId2 = std::to_string(r.id);
                p_bind_text(us, 1, sAid.c_str(), -1, NULL);
                p_bind_text(us, 2, sId2.c_str(), -1, NULL);
                p_step(us);
                if (p_finalize) p_finalize(us);
            }
        }
        out.push_back(r);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

bool DB::GetProject(int id, ProjectRow &outProject) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "SELECT id, name, directory, description, lang, version, created, last_updated, register_in_windows, app_icon_path, app_publisher, use_components, app_id FROM projects WHERE id = ?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    
    std::string idStr;
    { std::ostringstream os; os << id; idStr = os.str(); }
    if (p_bind_text) p_bind_text(stmt, 1, idStr.c_str(), -1, NULL);

    bool found = false;
    if (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        outProject.id = (int)p_col_int64(stmt, 0);
        const unsigned char *t0 = p_col_text(stmt, 1);
        const unsigned char *t1 = p_col_text(stmt, 2);
        const unsigned char *t2 = p_col_text(stmt, 3);
        const unsigned char *t3 = p_col_text(stmt, 4);
        const unsigned char *t4 = p_col_text(stmt, 5);
        const unsigned char *t5 = p_col_text(stmt, 6);
        const unsigned char *t6 = p_col_text(stmt, 7);
        const unsigned char *t7 = p_col_text(stmt, 9);
        const unsigned char *t8 = p_col_text(stmt, 10);
        outProject.name = Utf8ToW(t0 ? (const char*)t0 : "");
        outProject.directory = Utf8ToW(t1 ? (const char*)t1 : "");
        outProject.description = Utf8ToW(t2 ? (const char*)t2 : "");
        outProject.lang = Utf8ToW(t3 ? (const char*)t3 : "");
        outProject.version = Utf8ToW(t4 ? (const char*)t4 : "");
        outProject.created = t5 ? atoll((const char*)t5) : 0;
        outProject.last_updated = t6 ? atoll((const char*)t6) : 0;
        outProject.register_in_windows = (int)p_col_int64(stmt, 8);
        outProject.app_icon_path = Utf8ToW(t7 ? (const char*)t7 : "");
        outProject.app_publisher = Utf8ToW(t8 ? (const char*)t8 : "");
        outProject.use_components = (int)p_col_int64(stmt, 11);
        const unsigned char *t9 = p_col_text(stmt, 12);
        outProject.app_id = Utf8ToW(t9 ? (const char*)t9 : "");
        // Existing project with no GUID yet — generate one and persist it.
        if (outProject.app_id.empty() && outProject.id > 0) {
            if (p_finalize) p_finalize(stmt); stmt = NULL;
            p_close(db); db = NULL;
            outProject.app_id = GenerateAppId();
            // Re-open to persist the new GUID.
            if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) == 0) {
                const char* updSql = "UPDATE projects SET app_id=? WHERE id=?;";
                void* us = NULL;
                if (p_prepare(db, updSql, -1, &us, NULL) == 0) {
                    std::string sAid = WToUtf8(outProject.app_id), sId2 = std::to_string(outProject.id);
                    p_bind_text(us, 1, sAid.c_str(), -1, NULL);
                    p_bind_text(us, 2, sId2.c_str(), -1, NULL);
                    p_step(us);
                    if (p_finalize) p_finalize(us);
                }
                p_close(db);
            }
            return true;
        }
        found = true;
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return found;
}

bool DB::GetSetting(const std::wstring &key, std::wstring &outValue) {
    outValue.clear();
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "SELECT value FROM settings WHERE key = ?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string k = WToUtf8(key);
    if (p_bind_text) p_bind_text(stmt, 1, k.c_str(), -1, NULL);

    bool found = false;
    if (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        const unsigned char *val = p_col_text(stmt, 0);
        outValue = Utf8ToW(val ? (const char*)val : "");
        found = true;
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return found;
}

bool DB::SetSetting(const std::wstring &key, const std::wstring &value) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string k = WToUtf8(key);
    std::string v = WToUtf8(value);
    if (p_bind_text) p_bind_text(stmt, 1, k.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, v.c_str(), -1, NULL);

    int rc = p_step(stmt);
    (void)rc;
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

bool DB::UpdateProject(const ProjectRow &project) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "UPDATE projects SET name=?, directory=?, description=?, lang=?, version=?, last_updated=?, register_in_windows=?, app_icon_path=?, app_publisher=?, use_components=?, app_id=? WHERE id=?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }

    std::string sName   = WToUtf8(project.name);
    std::string sDir    = WToUtf8(project.directory);
    std::string sDesc   = WToUtf8(project.description);
    std::string sLang   = WToUtf8(project.lang);
    std::string sVer    = WToUtf8(project.version);
    std::string sIcon   = WToUtf8(project.app_icon_path);
    std::string sPub    = WToUtf8(project.app_publisher);
    std::string sReg    = std::to_string(project.register_in_windows);
    std::string sUseComp = std::to_string(project.use_components);
    std::string sAppId  = WToUtf8(project.app_id.empty() ? GenerateAppId() : project.app_id);
    std::ostringstream os; os << (long long)time(NULL); std::string sNow = os.str();
    std::string sId     = std::to_string(project.id);

    if (p_bind_text) p_bind_text(stmt, 1,  sName.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2,  sDir.c_str(),     -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3,  sDesc.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4,  sLang.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 5,  sVer.c_str(),     -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 6,  sNow.c_str(),     -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 7,  sReg.c_str(),     -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 8,  sIcon.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 9,  sPub.c_str(),     -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 10, sUseComp.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 11, sAppId.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 12, sId.c_str(),      -1, NULL);

    int rc2 = p_step(stmt); (void)rc2;
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

bool DB::DeleteRegistryEntriesForProject(int projectId) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "DELETE FROM registry_entries WHERE project_id = ?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sId = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sId.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

bool DB::InsertRegistryEntry(int projectId, const std::wstring &hive, const std::wstring &path,
                              const std::wstring &name, const std::wstring &type, const std::wstring &data,
                              const std::wstring &flags, const std::wstring &components) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags_o = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags_o, NULL) != 0) return false;

    const char *sql = "INSERT INTO registry_entries (project_id, hive, path, name, type, data, flags, components) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }

    std::string sId         = std::to_string(projectId);
    std::string sHive       = WToUtf8(hive);
    std::string sPath       = WToUtf8(path);
    std::string sName       = WToUtf8(name);
    std::string sType       = WToUtf8(type);
    std::string sData       = WToUtf8(data);
    std::string sFlags      = WToUtf8(flags);
    std::string sComponents = WToUtf8(components);

    if (p_bind_text) p_bind_text(stmt, 1, sId.c_str(),         -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, sHive.c_str(),       -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3, sPath.c_str(),       -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4, sName.c_str(),       -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 5, sType.c_str(),       -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 6, sData.c_str(),       -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 7, sFlags.c_str(),      -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 8, sComponents.c_str(), -1, NULL);

    int rc2 = p_step(stmt); (void)rc2;
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

std::vector<RegistryEntryRow> DB::GetRegistryEntriesForProject(int projectId) {
    std::vector<RegistryEntryRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;

    const char *sql = "SELECT id, hive, path, name, type, data, flags, components FROM registry_entries WHERE project_id = ? ORDER BY id ASC;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sId = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sId.c_str(), -1, NULL);

    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        RegistryEntryRow r;
        r.id         = (int)p_col_int64(stmt, 0);
        r.project_id = projectId;
        const unsigned char *c1 = p_col_text(stmt, 1);
        const unsigned char *c2 = p_col_text(stmt, 2);
        const unsigned char *c3 = p_col_text(stmt, 3);
        const unsigned char *c4 = p_col_text(stmt, 4);
        const unsigned char *c5 = p_col_text(stmt, 5);
        const unsigned char *c6 = p_col_text(stmt, 6);
        const unsigned char *c7 = p_col_text(stmt, 7);
        r.hive       = Utf8ToW(c1 ? (const char*)c1 : "");
        r.path       = Utf8ToW(c2 ? (const char*)c2 : "");
        r.name       = Utf8ToW(c3 ? (const char*)c3 : "");
        r.type       = Utf8ToW(c4 ? (const char*)c4 : "");
        r.data       = Utf8ToW(c5 ? (const char*)c5 : "");
        r.flags      = Utf8ToW(c6 ? (const char*)c6 : "");
        r.components = Utf8ToW(c7 ? (const char*)c7 : "");
        out.push_back(r);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

// ─── Component persistence ───────────────────────────────────────────────────

int DB::InsertComponent(const ComponentRow &comp) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return 0;

    const char *sql = "INSERT INTO components (project_id, display_name, description, notes_rtf, is_required, is_preselected, source_type, source_path, dest_path) VALUES (?,?,?,?,?,?,?,?,?);";  // 9 params
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return 0; }
    std::string sProjId  = std::to_string(comp.project_id);
    std::string sName    = WToUtf8(comp.display_name);
    std::string sDesc    = WToUtf8(comp.description);
    std::string sNotes   = WToUtf8(comp.notes_rtf);
    std::string sReq     = std::to_string(comp.is_required);
    std::string sPresel  = std::to_string(comp.is_preselected);
    std::string sType    = WToUtf8(comp.source_type);
    std::string sSrc     = WToUtf8(comp.source_path);
    std::string sDst     = WToUtf8(comp.dest_path);
    if (p_bind_text) p_bind_text(stmt, 1, sProjId.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, sName.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3, sDesc.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4, sNotes.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 5, sReq.c_str(),     -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 6, sPresel.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 7, sType.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 8, sSrc.c_str(),     -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 9, sDst.c_str(),     -1, NULL);
    int rc = p_step(stmt); (void)rc;
    int newId = (p_last_insert && p_finalize) ? (int)p_last_insert(db) : 0;
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return newId;
}

bool DB::UpdateComponent(const ComponentRow &comp) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "UPDATE components SET display_name=?, description=?, notes_rtf=?, is_required=?, is_preselected=?, source_type=?, source_path=?, dest_path=? WHERE id=?;";  // ?10=id
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sName  = WToUtf8(comp.display_name);
    std::string sDesc  = WToUtf8(comp.description);
    std::string sNotes = WToUtf8(comp.notes_rtf);
    std::string sReq    = std::to_string(comp.is_required);
    std::string sPresel = std::to_string(comp.is_preselected);
    std::string sType   = WToUtf8(comp.source_type);
    std::string sSrc    = WToUtf8(comp.source_path);
    std::string sDst    = WToUtf8(comp.dest_path);
    std::string sId     = std::to_string(comp.id);
    if (p_bind_text) p_bind_text(stmt, 1, sName.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, sDesc.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3, sNotes.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4, sReq.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 5, sPresel.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 6, sType.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 7, sSrc.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 8, sDst.c_str(),    -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 9, sId.c_str(),     -1, NULL);
    int rc = p_step(stmt); (void)rc;
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

std::vector<ComponentRow> DB::GetComponentsForProject(int projectId) {
    std::vector<ComponentRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;

    const char *sql = "SELECT id, display_name, description, notes_rtf, is_required, is_preselected, source_type, source_path, dest_path FROM components WHERE project_id=? ORDER BY id ASC;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sProjId = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sProjId.c_str(), -1, NULL);

    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        ComponentRow r;
        r.id         = (int)p_col_int64(stmt, 0);
        r.project_id = projectId;
        const unsigned char *c1 = p_col_text(stmt, 1);
        const unsigned char *c2 = p_col_text(stmt, 2);
        const unsigned char *c3 = p_col_text(stmt, 3);
        r.is_required    = (int)p_col_int64(stmt, 4);
        r.is_preselected = (int)p_col_int64(stmt, 5);
        const unsigned char *c6 = p_col_text(stmt, 6);
        const unsigned char *c7 = p_col_text(stmt, 7);
        const unsigned char *c8 = p_col_text(stmt, 8);
        r.display_name = Utf8ToW(c1 ? (const char*)c1 : "");
        r.description  = Utf8ToW(c2 ? (const char*)c2 : "");
        r.notes_rtf    = Utf8ToW(c3 ? (const char*)c3 : "");
        r.source_type  = Utf8ToW(c6 ? (const char*)c6 : "");
        r.source_path  = Utf8ToW(c7 ? (const char*)c7 : "");
        r.dest_path    = Utf8ToW(c8 ? (const char*)c8 : "");
        out.push_back(r);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

bool DB::DeleteComponentsForProject(int projectId) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "DELETE FROM components WHERE project_id=?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sProjId = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sProjId.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

bool DB::DeleteComponent(int id) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "DELETE FROM components WHERE id=?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sId = std::to_string(id);
    if (p_bind_text) p_bind_text(stmt, 1, sId.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

// ─── Component dependency persistence ────────────────────────────────────────

bool DB::InsertComponentDependency(int componentId, int dependsOnId) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char *sql = "INSERT OR IGNORE INTO component_dependencies (component_id, depends_on_id) VALUES (?,?);";
    void *stmt2 = NULL;
    if (p_prepare(db, sql, -1, &stmt2, NULL) != 0) { p_close(db); return false; }
    std::string sComp = std::to_string(componentId);
    std::string sDep  = std::to_string(dependsOnId);
    if (p_bind_text) p_bind_text(stmt2, 1, sComp.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt2, 2, sDep.c_str(),  -1, NULL);
    p_step(stmt2);
    if (p_finalize) p_finalize(stmt2);
    p_close(db); return true;
}

std::vector<int> DB::GetDependenciesForComponent(int componentId) {
    std::vector<int> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;
    const char *sql = "SELECT depends_on_id FROM component_dependencies WHERE component_id=? ORDER BY id ASC;";
    void *stmt2 = NULL;
    if (p_prepare(db, sql, -1, &stmt2, NULL) != 0) { p_close(db); return out; }
    std::string sComp = std::to_string(componentId);
    if (p_bind_text) p_bind_text(stmt2, 1, sComp.c_str(), -1, NULL);
    while (p_step(stmt2) == 100 /*SQLITE_ROW*/)
        out.push_back((int)p_col_int64(stmt2, 0));
    if (p_finalize) p_finalize(stmt2);
    p_close(db); return out;
}

bool DB::DeleteDependenciesForComponent(int componentId) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char *sql = "DELETE FROM component_dependencies WHERE component_id=?;";
    void *stmt2 = NULL;
    if (p_prepare(db, sql, -1, &stmt2, NULL) != 0) { p_close(db); return false; }
    std::string sComp = std::to_string(componentId);
    if (p_bind_text) p_bind_text(stmt2, 1, sComp.c_str(), -1, NULL);
    p_step(stmt2);
    if (p_finalize) p_finalize(stmt2);
    p_close(db); return true;
}

// ─── Shortcuts: menu nodes ───────────────────────────────────────────────────

bool DB::InsertScMenuNode(int projectId, const DB::ScMenuNodeRow& node) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char *sql = "INSERT OR REPLACE INTO sc_menu_nodes (id, project_id, parent_id, name) VALUES (?,?,?,?);";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sId   = std::to_string(node.id);
    std::string sPid  = std::to_string(projectId);
    std::string sPar  = std::to_string(node.parent_id);
    std::string sName = WToUtf8(node.name);
    if (p_bind_text) p_bind_text(stmt, 1, sId.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, sPid.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3, sPar.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4, sName.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db); return true;
}

bool DB::DeleteScMenuNodesForProject(int projectId) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char *sql = "DELETE FROM sc_menu_nodes WHERE project_id=?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sPid = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db); return true;
}

std::vector<DB::ScMenuNodeRow> DB::GetScMenuNodesForProject(int projectId) {
    std::vector<DB::ScMenuNodeRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;
    const char *sql = "SELECT id, parent_id, name FROM sc_menu_nodes WHERE project_id=? ORDER BY id ASC;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sPid = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        DB::ScMenuNodeRow r;
        r.id         = (int)p_col_int64(stmt, 0);
        r.project_id = projectId;
        r.parent_id  = (int)p_col_int64(stmt, 1);
        const unsigned char *c2 = p_col_text(stmt, 2);
        r.name = Utf8ToW(c2 ? (const char*)c2 : "");
        out.push_back(r);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db); return out;
}

// ─── Shortcuts: shortcut definitions ─────────────────────────────────────────

bool DB::InsertScShortcut(int projectId, const DB::ScShortcutRow& sc) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char *sql = "INSERT OR REPLACE INTO sc_shortcuts "
        "(id, project_id, type, sm_node_id, name, exe_path, working_dir, arguments, comment, hotkey, icon_path, icon_index, run_as_admin, pin_to_start, pin_to_taskbar) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sId   = std::to_string(sc.id);
    std::string sPid  = std::to_string(projectId);
    std::string sType = std::to_string(sc.type);
    std::string sSm   = std::to_string(sc.sm_node_id);
    std::string sName = WToUtf8(sc.name);
    std::string sExe  = WToUtf8(sc.exe_path);
    std::string sWd   = WToUtf8(sc.working_dir);
    std::string sArgs = WToUtf8(sc.arguments);
    std::string sCmt  = WToUtf8(sc.comment);
    std::string sHk   = WToUtf8(sc.hotkey);
    std::string sIcoP = WToUtf8(sc.icon_path);
    std::string sIcoI = std::to_string(sc.icon_index);
    std::string sAdm  = std::to_string(sc.run_as_admin);
    std::string sPinS = std::to_string(sc.pin_to_start);
    std::string sPinT = std::to_string(sc.pin_to_taskbar);
    if (p_bind_text) p_bind_text(stmt,  1, sId.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  2, sPid.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  3, sType.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  4, sSm.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  5, sName.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  6, sExe.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  7, sWd.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  8, sArgs.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt,  9, sCmt.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 10, sHk.c_str(),   -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 11, sIcoP.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 12, sIcoI.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 13, sAdm.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 14, sPinS.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 15, sPinT.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db); return true;
}

bool DB::DeleteScShortcutsForProject(int projectId) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char *sql = "DELETE FROM sc_shortcuts WHERE project_id=?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sPid = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db); return true;
}

std::vector<DB::ScShortcutRow> DB::GetScShortcutsForProject(int projectId) {
    std::vector<DB::ScShortcutRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL; int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;
    const char *sql = "SELECT id, type, sm_node_id, name, exe_path, working_dir, arguments, comment, hotkey, "
        "icon_path, icon_index, run_as_admin, pin_to_start, pin_to_taskbar FROM sc_shortcuts WHERE project_id=? ORDER BY id ASC;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sPid = std::to_string(projectId);
    if (p_bind_text) p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        DB::ScShortcutRow r;
        r.id          = (int)p_col_int64(stmt, 0);
        r.project_id  = projectId;
        r.type        = (int)p_col_int64(stmt, 1);
        r.sm_node_id  = (int)p_col_int64(stmt, 2);
        const unsigned char *c3 = p_col_text(stmt, 3);
        const unsigned char *c4 = p_col_text(stmt, 4);
        const unsigned char *c5 = p_col_text(stmt, 5);
        const unsigned char *c6 = p_col_text(stmt, 6);
        const unsigned char *c7 = p_col_text(stmt, 7);
        const unsigned char *c8 = p_col_text(stmt, 8);
        const unsigned char *c9 = p_col_text(stmt, 9);
        r.name        = Utf8ToW(c3 ? (const char*)c3 : "");
        r.exe_path    = Utf8ToW(c4 ? (const char*)c4 : "");
        r.working_dir = Utf8ToW(c5 ? (const char*)c5 : "");
        r.arguments   = Utf8ToW(c6 ? (const char*)c6 : "");
        r.comment     = Utf8ToW(c7 ? (const char*)c7 : "");
        r.hotkey      = Utf8ToW(c8 ? (const char*)c8 : "");
        r.icon_path   = Utf8ToW(c9 ? (const char*)c9 : "");
        r.icon_index  = (int)p_col_int64(stmt, 10);
        r.run_as_admin = (int)p_col_int64(stmt, 11);
        r.pin_to_start   = (int)p_col_int64(stmt, 12);
        r.pin_to_taskbar = (int)p_col_int64(stmt, 13);
        out.push_back(r);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db); return out;
}

// ── External dependency CRUD ──────────────────────────────────────────────────

int DB::InsertExternalDep(int projectId, const ExternalDep& dep)
{
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return -1;

    const char *sql =
        "INSERT INTO external_deps (project_id, display_name, is_required, delivery, "
        "install_order, detect_reg_key, detect_file_path, min_version, architecture, "
        "url, silent_args, sha256, license_path, license_text, credits_text, "
        "instructions, offline_behavior, download_timeout_sec, extra_exit_codes, max_version, required_components, detect_version_source) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return -1; }

    std::string sPid        = std::to_string(projectId);
    std::string sName       = WToUtf8(dep.display_name);
    std::string sIsReq      = std::to_string(dep.is_required ? 1 : 0);
    std::string sDelivery   = std::to_string((int)dep.delivery);
    std::string sOrder      = std::to_string(dep.install_order);
    std::string sRegKey     = WToUtf8(dep.detect_reg_key);
    std::string sFilePath   = WToUtf8(dep.detect_file_path);
    std::string sMinVer     = WToUtf8(dep.min_version);
    std::string sArch       = std::to_string((int)dep.architecture);
    std::string sUrl        = WToUtf8(dep.url);
    std::string sSilent     = WToUtf8(dep.silent_args);
    std::string sSha256     = WToUtf8(dep.sha256);
    std::string sLicPath    = WToUtf8(dep.license_path);
    std::string sLicText    = WToUtf8(dep.license_text);
    std::string sCredits    = WToUtf8(dep.credits_text);
    std::string sInstr      = "";  // instructions stored in dep_instructions table
    std::string sOffline    = std::to_string((int)dep.offline_behavior);
    std::string sTimeout    = std::to_string(dep.download_timeout_sec);
    std::string sExitCodes  = WToUtf8(dep.extra_exit_codes);
    std::string sMaxVer     = WToUtf8(dep.max_version);
    std::string sReqComp    = WToUtf8(dep.required_components);
    std::string sVerSrc    = std::to_string((int)dep.detect_version_source);

    p_bind_text(stmt,  1, sPid.c_str(),       -1, NULL);
    p_bind_text(stmt,  2, sName.c_str(),      -1, NULL);
    p_bind_text(stmt,  3, sIsReq.c_str(),     -1, NULL);
    p_bind_text(stmt,  4, sDelivery.c_str(),  -1, NULL);
    p_bind_text(stmt,  5, sOrder.c_str(),     -1, NULL);
    p_bind_text(stmt,  6, sRegKey.c_str(),    -1, NULL);
    p_bind_text(stmt,  7, sFilePath.c_str(),  -1, NULL);
    p_bind_text(stmt,  8, sMinVer.c_str(),    -1, NULL);
    p_bind_text(stmt,  9, sArch.c_str(),      -1, NULL);
    p_bind_text(stmt, 10, sUrl.c_str(),       -1, NULL);
    p_bind_text(stmt, 11, sSilent.c_str(),    -1, NULL);
    p_bind_text(stmt, 12, sSha256.c_str(),    -1, NULL);
    p_bind_text(stmt, 13, sLicPath.c_str(),   -1, NULL);
    p_bind_text(stmt, 14, sLicText.c_str(),   -1, NULL);
    p_bind_text(stmt, 15, sCredits.c_str(),   -1, NULL);
    p_bind_text(stmt, 16, sInstr.c_str(),     -1, NULL);
    p_bind_text(stmt, 17, sOffline.c_str(),    -1, NULL);
    p_bind_text(stmt, 18, sTimeout.c_str(),    -1, NULL);
    p_bind_text(stmt, 19, sExitCodes.c_str(),  -1, NULL);
    p_bind_text(stmt, 20, sMaxVer.c_str(),      -1, NULL);
    p_bind_text(stmt, 21, sReqComp.c_str(),     -1, NULL);
    p_bind_text(stmt, 22, sVerSrc.c_str(),      -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);

    int newId = -1;
    const char *idSql = "SELECT last_insert_rowid();";
    void *idStmt = NULL;
    if (p_prepare(db, idSql, -1, &idStmt, NULL) == 0) {
        if (p_step(idStmt) == 100) newId = (int)p_col_int64(idStmt, 0);
        if (p_finalize) p_finalize(idStmt);
    }

    // Insert each instruction page into dep_instructions
    if (newId > 0 && !dep.instructions_list.empty()) {
        const char *instrSql =
            "INSERT INTO dep_instructions (dep_id, project_id, sort_order, rtf_text) "
            "VALUES (?,?,?,?);";
        for (int i = 0; i < (int)dep.instructions_list.size(); i++) {
            void *iStmt = NULL;
            if (p_prepare(db, instrSql, -1, &iStmt, NULL) != 0) continue;
            std::string sDepId  = std::to_string(newId);
            std::string sProjId = std::to_string(projectId);
            std::string sOrd    = std::to_string(i);
            std::string sRtf    = WToUtf8(dep.instructions_list[i]);
            p_bind_text(iStmt, 1, sDepId.c_str(),  -1, NULL);
            p_bind_text(iStmt, 2, sProjId.c_str(), -1, NULL);
            p_bind_text(iStmt, 3, sOrd.c_str(),    -1, NULL);
            p_bind_text(iStmt, 4, sRtf.c_str(),    -1, NULL);
            p_step(iStmt);
            if (p_finalize) p_finalize(iStmt);
        }
    }

    p_close(db);
    return newId;
}

bool DB::DeleteExternalDepsForProject(int projectId)
{
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    // Delete instruction pages first (no FK cascade enforced)
    const char *sqlInstr = "DELETE FROM dep_instructions WHERE project_id=?;";
    void *stmtI = NULL;
    if (p_prepare(db, sqlInstr, -1, &stmtI, NULL) == 0) {
        std::string sPid = std::to_string(projectId);
        p_bind_text(stmtI, 1, sPid.c_str(), -1, NULL);
        p_step(stmtI);
        if (p_finalize) p_finalize(stmtI);
    }
    const char *sql = "DELETE FROM external_deps WHERE project_id=?;";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sPid = std::to_string(projectId);
    p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

std::vector<ExternalDep> DB::GetExternalDepsForProject(int projectId)
{
    std::vector<ExternalDep> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;

    const char *sql =
        "SELECT id, display_name, is_required, delivery, install_order, "
        "detect_reg_key, detect_file_path, min_version, architecture, "
        "url, silent_args, sha256, license_path, license_text, "
        "credits_text, instructions, offline_behavior, download_timeout_sec, extra_exit_codes, max_version, required_components, detect_version_source "
        "FROM external_deps WHERE project_id=? ORDER BY install_order ASC, id ASC";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sPid = std::to_string(projectId);
    p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);

    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        ExternalDep d;
        d.project_id      = projectId;
        d.id              = (int)p_col_int64(stmt, 0);
        auto T = [&](int col) -> std::wstring {
            const unsigned char *c = p_col_text(stmt, col);
            return Utf8ToW(c ? (const char*)c : "");
        };
        d.display_name     = T(1);
        d.is_required      = (p_col_int64(stmt, 2) != 0);
        d.delivery         = (DepDelivery)(int)p_col_int64(stmt, 3);
        d.install_order    = (int)p_col_int64(stmt, 4);
        d.detect_reg_key   = T(5);
        d.detect_file_path = T(6);
        d.min_version      = T(7);
        d.architecture     = (DepArch)(int)p_col_int64(stmt, 8);
        d.url              = T(9);
        d.silent_args      = T(10);
        d.sha256           = T(11);
        d.license_path     = T(12);
        d.license_text     = T(13);
        d.credits_text     = T(14);
        d.offline_behavior        = (DepOffline)(int)p_col_int64(stmt, 16);
        d.download_timeout_sec    = (int)p_col_int64(stmt, 17);
        d.extra_exit_codes        = T(18);
        d.max_version             = T(19);
        d.required_components     = T(20);
        d.detect_version_source   = (DepVersionSource)(int)p_col_int64(stmt, 21);
        // instructions_list populated below after we know d.id
        out.push_back(d);
    }
    if (p_finalize) p_finalize(stmt);

    // Load instruction pages for each dep (ordered by sort_order)
    const char *iSql =
        "SELECT rtf_text FROM dep_instructions "
        "WHERE dep_id=? ORDER BY sort_order ASC;";
    for (auto& d : out) {
        void *iStmt = NULL;
        if (p_prepare(db, iSql, -1, &iStmt, NULL) != 0) continue;
        std::string sId = std::to_string(d.id);
        p_bind_text(iStmt, 1, sId.c_str(), -1, NULL);
        while (p_step(iStmt) == 100) {
            const unsigned char *c = p_col_text(iStmt, 0);
            d.instructions_list.push_back(Utf8ToW(c ? (const char*)c : ""));
        }
        if (p_finalize) p_finalize(iStmt);
    }

    p_close(db);
    return out;
}

// ── Installer dialog persistence ──────────────────────────────────────────────

bool DB::UpsertInstallerDialog(int projectId, int dialogType, const std::wstring& rtf)
{
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char* sql =
        "INSERT OR REPLACE INTO installer_dialogs (project_id, dialog_type, content_rtf) "
        "VALUES (?, ?, ?);";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sPid  = std::to_string(projectId);
    std::string sType = std::to_string(dialogType);
    std::string sRtf  = WToUtf8(rtf);
    p_bind_text(stmt, 1, sPid.c_str(),  -1, NULL);
    p_bind_text(stmt, 2, sType.c_str(), -1, NULL);
    p_bind_text(stmt, 3, sRtf.c_str(),  -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

bool DB::DeleteInstallerDialogsForProject(int projectId)
{
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char* sql = "DELETE FROM installer_dialogs WHERE project_id=?;";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sPid = std::to_string(projectId);
    p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

std::vector<std::pair<int,std::wstring>> DB::GetAllDialogDefaults()
{
    std::vector<std::pair<int,std::wstring>> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;
    const char* sql =
        "SELECT dialog_type, content_rtf FROM dialog_defaults ORDER BY dialog_type ASC;";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        int type = (int)p_col_int64(stmt, 0);
        const unsigned char* c = p_col_text(stmt, 1);
        out.push_back({ type, Utf8ToW(c ? (const char*)c : "") });
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

// ── License template queries ──────────────────────────────────────────────────

// Returns all templates as (id, name, img_file) triples, ordered by id.
std::vector<DB::LicenseTemplateInfo> DB::GetAllLicenseTemplates()
{
    std::vector<DB::LicenseTemplateInfo> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;
    const char* sql = "SELECT id, name, img_file FROM license_templates ORDER BY id ASC;";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        DB::LicenseTemplateInfo info;
        info.id       = (int)p_col_int64(stmt, 0);
        const unsigned char* n = p_col_text(stmt, 1);
        const unsigned char* f = p_col_text(stmt, 2);
        info.name     = Utf8ToW(n ? (const char*)n : "");
        info.img_file = Utf8ToW(f ? (const char*)f : "");
        out.push_back(info);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

// Returns the RTF content of the given template id, or empty string if not found.
std::wstring DB::GetLicenseTemplateRtf(int id)
{
    std::wstring out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;
    const char* sql = "SELECT content_rtf FROM license_templates WHERE id=?;";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sId = std::to_string(id);
    p_bind_text(stmt, 1, sId.c_str(), -1, NULL);
    if (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        const unsigned char* c = p_col_text(stmt, 0);
        out = Utf8ToW(c ? (const char*)c : "");
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

// ── Script CRUD ───────────────────────────────────────────────────────────────

int DB::InsertScript(int projectId, const ScriptRow& s)
{
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return -1;

    const char* sql =
        "INSERT INTO scripts (project_id, name, type, content, when_to_run, "
        "run_hidden, wait_for_completion, description, also_uninstall, sort_order) "
        "VALUES (?,?,?,?,?,?,?,?,?,?);";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return -1; }

    std::string sPid   = std::to_string(projectId);
    std::string sName  = WToUtf8(s.name);
    std::string sType  = std::to_string(s.type);
    std::string sCont  = WToUtf8(s.content);
    std::string sWhen  = std::to_string(s.when_to_run);
    std::string sHid   = std::to_string(s.run_hidden);
    std::string sWait  = std::to_string(s.wait_for_completion);
    std::string sDesc  = WToUtf8(s.description);
    std::string sAlso  = std::to_string(s.also_uninstall);
    std::string sSort  = std::to_string(s.sort_order);

    p_bind_text(stmt,  1, sPid.c_str(),  -1, NULL);
    p_bind_text(stmt,  2, sName.c_str(), -1, NULL);
    p_bind_text(stmt,  3, sType.c_str(), -1, NULL);
    p_bind_text(stmt,  4, sCont.c_str(), -1, NULL);
    p_bind_text(stmt,  5, sWhen.c_str(), -1, NULL);
    p_bind_text(stmt,  6, sHid.c_str(),  -1, NULL);
    p_bind_text(stmt,  7, sWait.c_str(), -1, NULL);
    p_bind_text(stmt,  8, sDesc.c_str(), -1, NULL);
    p_bind_text(stmt,  9, sAlso.c_str(), -1, NULL);
    p_bind_text(stmt, 10, sSort.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);

    int newId = -1;
    const char* idSql = "SELECT last_insert_rowid();";
    void* idStmt = NULL;
    if (p_prepare(db, idSql, -1, &idStmt, NULL) == 0) {
        if (p_step(idStmt) == 100) newId = (int)p_col_int64(idStmt, 0);
        if (p_finalize) p_finalize(idStmt);
    }
    p_close(db);
    return newId;
}

bool DB::DeleteScriptsForProject(int projectId)
{
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;
    const char* sql = "DELETE FROM scripts WHERE project_id=?;";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }
    std::string sPid = std::to_string(projectId);
    p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

std::vector<DB::ScriptRow> DB::GetScriptsForProject(int projectId)
{
    std::vector<ScriptRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;

    const char* sql =
        "SELECT id, name, type, content, when_to_run, run_hidden, "
        "wait_for_completion, description, also_uninstall, sort_order "
        "FROM scripts WHERE project_id=? ORDER BY sort_order ASC, id ASC;";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sPid = std::to_string(projectId);
    p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);

    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        ScriptRow r;
        r.project_id          = projectId;
        r.id                  = (int)p_col_int64(stmt, 0);
        auto T = [&](int col) -> std::wstring {
            const unsigned char* c = p_col_text(stmt, col);
            return Utf8ToW(c ? (const char*)c : "");
        };
        r.name                = T(1);
        r.type                = (int)p_col_int64(stmt, 2);
        r.content             = T(3);
        r.when_to_run         = (int)p_col_int64(stmt, 4);
        r.run_hidden          = (int)p_col_int64(stmt, 5);
        r.wait_for_completion = (int)p_col_int64(stmt, 6);
        r.description         = T(7);
        r.also_uninstall      = (int)p_col_int64(stmt, 8);
        r.sort_order          = (int)p_col_int64(stmt, 9);
        out.push_back(r);
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}

std::vector<std::pair<int,std::wstring>> DB::GetInstallerDialogsForProject(int projectId)
{
    std::vector<std::pair<int,std::wstring>> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void* db = NULL;
    int flags = 0x00000002 | 0x00000004;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;
    const char* sql =
        "SELECT dialog_type, content_rtf FROM installer_dialogs "
        "WHERE project_id=? ORDER BY dialog_type ASC;";
    void* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return out; }
    std::string sPid = std::to_string(projectId);
    p_bind_text(stmt, 1, sPid.c_str(), -1, NULL);
    while (p_step(stmt) == 100 /*SQLITE_ROW*/) {
        int type = (int)p_col_int64(stmt, 0);
        const unsigned char* c = p_col_text(stmt, 1);
        out.push_back({ type, Utf8ToW(c ? (const char*)c : "") });
    }
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return out;
}
