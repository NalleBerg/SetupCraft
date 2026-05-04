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
        // RTF colour table shared by all templates:
        //   \cf1 = blue title  \cf2 = dark body  \cf3 = grey credit  \cf4 = dark-red warning
        #define LT_COLORTBL \
            "{\\colortbl ;\\red0\\green70\\blue140;\\red40\\green40\\blue40;" \
            "\\red100\\green100\\blue100;\\red139\\green0\\blue0;}"
        #define LT_HEAD  "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss\\fcharset0 Arial;}}" LT_COLORTBL "\\f0"
        #define LT_TITLE(t)  "\\pard\\qc\\sb0\\sa60\\cf1{\\b\\fs28 " t "}\\par"
        #define LT_SUB(s)    "\\pard\\qc\\sb0\\sa200\\cf1\\fs18 " s "\\par"
        #define LT_BODY      "\\pard\\ql\\sb0\\sa120\\cf2\\fs20 "
        #define LT_WARN(w)   "{\\cf4\\b " w "}"
        #define LT_CREDIT    "\\pard\\ql\\sb120\\sa0\\cf3\\fs18\\i <<LicenseCreditNote>>\\i0\\par}"

        struct { int id; const char* name; const char* spdx; const char* img; const char* rtf; } kLT[] = {

            // ── 0: The Unlicense ────────────────────────────────────────────
            { 0, "The Unlicense (Public Domain)", "Unlicense", "",
              LT_HEAD
              LT_TITLE("Public Domain License")
              LT_SUB("The Unlicense")
              LT_BODY "<<AppName>> is released into the public domain.\\par "
              "Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, "
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
              "\\par "
              LT_CREDIT
            },

            // ── 1: MIT ──────────────────────────────────────────────────────
            { 1, "MIT License", "MIT", "MIT-cropped-OSI-horizontal-large.png",
              LT_HEAD
              LT_TITLE("MIT License")
              LT_SUB("SPDX: MIT")
              LT_BODY "Copyright (c) <<Year>> <<AppName>>\\par "
              "Permission is hereby granted, free of charge, to any person obtaining a copy of this "
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
              "\\par "
              LT_CREDIT
            },

            // ── 2: Apache 2.0 ───────────────────────────────────────────────
            { 2, "Apache License 2.0", "Apache-2.0", "apache.png",
              LT_HEAD
              LT_TITLE("Apache License")
              LT_SUB("Version 2.0, January 2004  |  SPDX: Apache-2.0")
              LT_BODY "Copyright <<Year>> <<AppName>>\\par "
              "Licensed under the Apache License, Version 2.0 (the \\\"License\\\"); you may not use "
              "this file except in compliance with the License.\\par "
              "You may obtain a copy of the License at:\\par "
              "{\\cf1\\ul https://www.apache.org/licenses/LICENSE-2.0}\\par "
              "Unless required by applicable law or agreed to in writing, software distributed under "
              "the License is distributed on an \\\"AS IS\\\" BASIS, WITHOUT WARRANTIES OR CONDITIONS "
              "OF ANY KIND, either express or implied. See the License for the specific language governing "
              "permissions and limitations under the License.\\par "
              "TERMS AND CONDITIONS\\par "
              "{\\b 1. Definitions.}\\par "
              "\\\"License\\\" shall mean the terms and conditions for use, reproduction, and distribution. "
              "\\\"Licensor\\\" shall mean the copyright owner or entity authorised by the copyright owner. "
              "\\\"Legal Entity\\\" shall mean the union of the acting entity and all other entities that "
              "control, are controlled by, or are under common control with that entity.\\par "
              "{\\b 2. Grant of Copyright License.} Subject to the terms and conditions of this License, "
              "each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, "
              "royalty-free, irrevocable copyright license to reproduce, prepare Derivative Works of, "
              "publicly display, publicly perform, sublicense, and distribute the Work and such Derivative Works.\\par "
              "{\\b 3. Grant of Patent License.} Subject to the terms and conditions of this License, "
              "each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, "
              "royalty-free, irrevocable patent license to make, use, sell, offer for sale, import, "
              "and otherwise transfer the Work.\\par "
              "{\\b 8. Limitation of Liability.} "
              LT_WARN("IN NO EVENT AND UNDER NO LEGAL THEORY, WHETHER IN TORT (INCLUDING NEGLIGENCE), "
                      "CONTRACT, OR OTHERWISE, UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING, "
                      "SHALL ANY CONTRIBUTOR BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY DIRECT, INDIRECT, "
                      "SPECIAL, INCIDENTAL, OR EXEMPLARY DAMAGES OF ANY CHARACTER ARISING AS A RESULT OF "
                      "THIS LICENSE OR OUT OF THE USE OR INABILITY TO USE THE WORK.")
              "\\par "
              LT_CREDIT
            },

            // ── 3: GPL v2 ────────────────────────────────────────────────────
            { 3, "GNU General Public License v2.0", "GPL-2.0-only", "GnuLogo.bmp",
              LT_HEAD
              LT_TITLE("GNU General Public License")
              LT_SUB("Version 2, June 1991  |  SPDX: GPL-2.0-only")
              LT_BODY "Copyright (C) <<Year>> <<AppName>>\\par "
              "Everyone is permitted to copy and distribute verbatim copies of this license document, "
              "but changing it is not allowed.\\par "
              "{\\b Preamble}\\par "
              "The GNU General Public License is a free, copyleft license for software and other "
              "kinds of works. When we speak of free software, we are referring to freedom, not price. "
              "Our General Public Licenses are designed to make sure that you have the freedom to "
              "distribute copies of free software (and charge for them if you wish), that you receive "
              "source code or can get it if you want it, that you can change the software or use pieces "
              "of it in new free programs, and that you know you can do these things.\\par "
              "{\\b Terms and Conditions}\\par "
              "{\\b 0.} This License applies to any program or other work which contains a notice placed "
              "by the copyright holder saying it may be distributed under the terms of this General "
              "Public License. The \\\"Program\\\", below, refers to any such program or work.\\par "
              "{\\b 1.} You may copy and distribute verbatim copies of the Program's source code as you "
              "receive it, in any medium, provided that you conspicuously and appropriately publish on "
              "each copy an appropriate copyright notice and disclaimer of warranty.\\par "
              "{\\b 2.} You may modify your copy or copies of the Program or any portion of it, thus "
              "forming a work based on the Program, and copy and distribute such modifications or work "
              "under the terms of Section 1 above, provided that you also cause the whole of any work "
              "you distribute to be licensed at no charge to all third parties under the terms of this License.\\par "
              LT_WARN("NO WARRANTY. THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, "
                      "BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF MERCHANTABILITY "
                      "OR FITNESS FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND "
                      "PERFORMANCE OF THE PROGRAM IS WITH YOU. SEE THE GNU GENERAL PUBLIC LICENSE FOR "
                      "FULL DETAILS.")
              "\\par "
              LT_CREDIT
            },

            // ── 4: GPL v3 ────────────────────────────────────────────────────
            { 4, "GNU General Public License v3.0", "GPL-3.0-only", "GnuLogo.bmp",
              LT_HEAD
              LT_TITLE("GNU General Public License")
              LT_SUB("Version 3, 29 June 2007  |  SPDX: GPL-3.0-only")
              LT_BODY "Copyright (C) <<Year>> <<AppName>>\\par "
              "Everyone is permitted to copy and distribute verbatim copies of this license document, "
              "but changing it is not allowed.\\par "
              "{\\b Preamble}\\par "
              "The GNU General Public License is a free, copyleft license for software and other kinds "
              "of works. The licenses for most software and other practical works are designed to take "
              "away your freedom to share and change the works. By contrast, the GNU General Public "
              "License is intended to guarantee your freedom to share and change all versions of a "
              "program — to make sure it remains free software for all its users.\\par "
              "{\\b Terms and Conditions}\\par "
              "{\\b 0. Definitions.} \\\"The Program\\\" refers to any copyrightable work licensed "
              "under this License.\\par "
              "{\\b 2. Basic Permissions.} All rights granted under this License are granted for the "
              "term of copyright on the Program, and are irrevocable provided the stated conditions "
              "are met. This License explicitly affirms your unlimited permission to run the unmodified "
              "Program.\\par "
              "{\\b 4. Conveying Verbatim Copies.} You may convey verbatim copies of the Program's "
              "source code as you receive it, in any medium, provided that you conspicuously and "
              "appropriately publish on each copy an appropriate copyright notice.\\par "
              "{\\b 5. Conveying Modified Source Versions.} You may convey a work based on the Program, "
              "or the modifications to produce it from the Program, in the form of source code under "
              "the terms of section 4.\\par "
              LT_WARN("15. Disclaimer of Warranty. THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT "
                      "PERMITTED BY APPLICABLE LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT "
                      "HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM \\\"AS IS\\\" WITHOUT WARRANTY OF "
                      "ANY KIND.")
              "\\par "
              LT_CREDIT
            },

            // ── 5: LGPL v2.1 ─────────────────────────────────────────────────
            { 5, "GNU Lesser General Public License v2.1", "LGPL-2.1-only", "GnuLogo.bmp",
              LT_HEAD
              LT_TITLE("GNU Lesser General Public License")
              LT_SUB("Version 2.1, February 1999  |  SPDX: LGPL-2.1-only")
              LT_BODY "Copyright (C) <<Year>> <<AppName>>\\par "
              "Everyone is permitted to copy and distribute verbatim copies of this license document, "
              "but changing it is not allowed.\\par "
              "{\\b Preamble}\\par "
              "The licenses for most software are designed to take away your freedom to share and change it. "
              "By contrast, the GNU Lesser General Public License is intended to permit developers and "
              "companies to use and link to the library even if they are not releasing their own software "
              "as free software. This license, the Lesser General Public License, applies to some specially "
              "designated software — typically libraries — of the Free Software Foundation and of other authors "
              "who decide to use it.\\par "
              "{\\b Terms and Conditions}\\par "
              "{\\b 0.} This License Agreement applies to any software library or other program which "
              "contains a notice placed by the copyright holder or other authorised party saying it may "
              "be distributed under the terms of this Lesser General Public License (also called \\\"this License\\\").\\par "
              "{\\b 1.} You may copy and distribute verbatim copies of the Library's complete source code "
              "as you receive it, in any medium, provided that you conspicuously and appropriately publish "
              "on each copy an appropriate copyright notice and disclaimer of warranty.\\par "
              "{\\b 2.} You may modify your copy or copies of the Library or any portion of it, thus forming "
              "a work based on the Library, and copy and distribute such modifications or work under the terms "
              "of Section 1, provided that you also meet all of these conditions:\\par "
              LT_WARN("NO WARRANTY. THIS LIBRARY IS PROVIDED \\\"AS IS\\\" WITHOUT WARRANTY OF ANY KIND, "
                      "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
                      "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.")
              "\\par "
              LT_CREDIT
            },

            // ── 6: LGPL v3 ───────────────────────────────────────────────────
            { 6, "GNU Lesser General Public License v3.0", "LGPL-3.0-only", "GnuLogo.bmp",
              LT_HEAD
              LT_TITLE("GNU Lesser General Public License")
              LT_SUB("Version 3, 29 June 2007  |  SPDX: LGPL-3.0-only")
              LT_BODY "Copyright (C) <<Year>> <<AppName>>\\par "
              "The GNU Lesser General Public License incorporates the terms and conditions of version 3 "
              "of the GNU General Public License, supplemented by the additional permissions listed below.\\par "
              "{\\b Additional Definitions.} As used herein, \\\"this License\\\" refers to version 3 "
              "of the GNU Lesser General Public License, and the \\\"GNU GPL\\\" refers to version 3 of "
              "the GNU General Public License.\\par "
              "{\\b 0. Additional Definitions.} \\\"The Library\\\" means a covered work governed by "
              "this License, other than an Application or a Combined Work.\\par "
              "{\\b 1. Exception to Section 3 of the GNU GPL.} You may convey a Combined Work under "
              "terms of your choice that, taken together, effectively do not restrict modification of "
              "the parts of the Library contained in the Combined Work and reverse engineering for "
              "debugging such modifications, if you also do each of the following:\\par "
              LT_WARN("THERE IS NO WARRANTY FOR THE LIBRARY, TO THE EXTENT PERMITTED BY APPLICABLE LAW. "
                      "EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES "
                      "PROVIDE THE LIBRARY \\\"AS IS\\\" WITHOUT WARRANTY OF ANY KIND.")
              "\\par "
              LT_CREDIT
            },

            // ── 7: AGPL v3 ───────────────────────────────────────────────────
            { 7, "GNU Affero General Public License v3.0", "AGPL-3.0-only", "GnuLogo.bmp",
              LT_HEAD
              LT_TITLE("GNU Affero General Public License")
              LT_SUB("Version 3, 19 November 2007  |  SPDX: AGPL-3.0-only")
              LT_BODY "Copyright (C) <<Year>> <<AppName>>\\par "
              "Everyone is permitted to copy and distribute verbatim copies of this license document, "
              "but changing it is not allowed.\\par "
              "{\\b Preamble}\\par "
              "The GNU Affero General Public License is a free, copyleft license for software and "
              "other kinds of works, specifically designed to ensure cooperation with the community in "
              "the case of network server software. When we speak of free software, we are referring to "
              "freedom, not price.\\par "
              "The GNU Affero General Public License is designed specifically to ensure that, in such "
              "cases, the modified source code becomes available to the community. It requires the "
              "operator of a network server to provide the source code of the modified version running "
              "there to the users of that server.\\par "
              "{\\b 13. Remote Network Interaction.} Notwithstanding any other provision of this License, "
              "if you modify the Program, your modified version must prominently offer all users interacting "
              "with it remotely through a computer network an opportunity to receive the Corresponding Source.\\par "
              LT_WARN("THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE LAW. "
                      "EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES "
                      "PROVIDE THE PROGRAM \\\"AS IS\\\" WITHOUT WARRANTY OF ANY KIND.")
              "\\par "
              LT_CREDIT
            },

            // ── 8: BSD 2-Clause ──────────────────────────────────────────────
            { 8, "BSD 2-Clause \"Simplified\" License", "BSD-2-Clause", "BSD.png",
              LT_HEAD
              LT_TITLE("BSD 2-Clause License")
              LT_SUB("\\\"Simplified\\\"  |  SPDX: BSD-2-Clause")
              LT_BODY "Copyright (c) <<Year>>, <<AppName>>\\par "
              "Redistribution and use in source and binary forms, with or without modification, are "
              "permitted provided that the following conditions are met:\\par "
              "{\\b 1.} Redistributions of source code must retain the above copyright notice, this "
              "list of conditions and the following disclaimer.\\par "
              "{\\b 2.} Redistributions in binary form must reproduce the above copyright notice, this "
              "list of conditions and the following disclaimer in the documentation and/or other "
              "materials provided with the distribution.\\par "
              LT_WARN("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \\\"AS IS\\\" "
                      "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED "
                      "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. "
                      "IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, "
                      "INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT "
                      "NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR "
                      "PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, "
                      "WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT ARISING FROM IN ANY WAY OUT OF THE "
                      "USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.")
              "\\par "
              LT_CREDIT
            },

            // ── 9: BSD 3-Clause ──────────────────────────────────────────────
            { 9, "BSD 3-Clause \"New\" License", "BSD-3-Clause", "BSD.png",
              LT_HEAD
              LT_TITLE("BSD 3-Clause License")
              LT_SUB("\\\"New\\\" / \\\"Revised\\\"  |  SPDX: BSD-3-Clause")
              LT_BODY "Copyright (c) <<Year>>, <<AppName>>\\par "
              "Redistribution and use in source and binary forms, with or without modification, are "
              "permitted provided that the following conditions are met:\\par "
              "{\\b 1.} Redistributions of source code must retain the above copyright notice, this "
              "list of conditions and the following disclaimer.\\par "
              "{\\b 2.} Redistributions in binary form must reproduce the above copyright notice, this "
              "list of conditions and the following disclaimer in the documentation and/or other "
              "materials provided with the distribution.\\par "
              "{\\b 3.} Neither the name of the copyright holder nor the names of its contributors may "
              "be used to endorse or promote products derived from this software without specific prior "
              "written permission.\\par "
              LT_WARN("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \\\"AS IS\\\" "
                      "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED "
                      "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. "
                      "IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, "
                      "INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT "
                      "NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR "
                      "PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, "
                      "WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT ARISING FROM IN ANY WAY OUT OF THE "
                      "USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.")
              "\\par "
              LT_CREDIT
            },

            // ── 10: ISC ──────────────────────────────────────────────────────
            { 10, "ISC License", "ISC", "OpeenSourceInitiative.png",
              LT_HEAD
              LT_TITLE("ISC License")
              LT_SUB("SPDX: ISC")
              LT_BODY "Copyright (c) <<Year>>, <<AppName>>\\par "
              "Permission to use, copy, modify, and/or distribute this software for any purpose with "
              "or without fee is hereby granted, provided that the above copyright notice and this "
              "permission notice appear in all copies.\\par "
              LT_WARN("THE SOFTWARE IS PROVIDED \\\"AS IS\\\" AND THE AUTHOR DISCLAIMS ALL WARRANTIES "
                      "WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY "
                      "AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, "
                      "INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS "
                      "OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER "
                      "TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE "
                      "OF THIS SOFTWARE.")
              "\\par "
              LT_CREDIT
            },

            // ── 11: MPL 2.0 ──────────────────────────────────────────────────
            { 11, "Mozilla Public License 2.0", "MPL-2.0", "Mozilla.png",
              LT_HEAD
              LT_TITLE("Mozilla Public License")
              LT_SUB("Version 2.0  |  SPDX: MPL-2.0")
              LT_BODY "1. Definitions\\par "
              "{\\b 1.1. \\\"Contributor\\\"} means each individual or legal entity that creates, "
              "contributes to the creation of, or owns Covered Software.\\par "
              "{\\b 1.2. \\\"Contributor Version\\\"} means the combination of the Contributions of "
              "others (if any) used by a Contributor and that particular Contributor's Contribution.\\par "
              "{\\b 1.3. \\\"Contribution\\\"} means Covered Software of a particular Contributor.\\par "
              "{\\b 1.4. \\\"Covered Software\\\"} means Source Code Form to which the initial "
              "Contributor has attached the notice in Exhibit A, the Executable Form of such Source "
              "Code Form, and Modifications of such Source Code Form, in each case including portions thereof.\\par "
              "{\\b 2. License Grants and Conditions}\\par "
              "{\\b 2.1. Grants.} Each Contributor hereby grants You a world-wide, royalty-free, "
              "non-exclusive license: (a) under intellectual property rights (other than patent or "
              "trademark) Licensable by such Contributor to use, reproduce, make available, modify, "
              "display, perform, distribute, and otherwise exploit its Contributions, either on an "
              "unmodified basis, with Modifications, or as part of a Larger Work; and (b) under "
              "Patent Claims of such Contributor to make, use, sell, offer for sale, have made, "
              "import, and otherwise transfer either its Contributions or its Contributor Version.\\par "
              LT_WARN("3.3. If You choose to distribute the Covered Software under a version of this "
                      "License other than the one currently available at mozilla.org/MPL/2.0/, You must "
                      "include a prominent notice stating that this version of the License has been superseded.")
              "\\par "
              LT_CREDIT
            },

            // ── 12: Boost Software License ───────────────────────────────────
            { 12, "Boost Software License 1.0", "BSL-1.0", "BSL-1.0 (Boost).png",
              LT_HEAD
              LT_TITLE("Boost Software License")
              LT_SUB("Version 1.0  |  SPDX: BSL-1.0")
              LT_BODY "Copyright (c) <<Year>>, <<AppName>>\\par "
              "Boost Software License — Version 1.0 — August 17th, 2003\\par "
              "Permission is hereby granted, free of charge, to any person or organisation obtaining "
              "a copy of the software and accompanying documentation covered by this license (the \\\"Software\\\") "
              "to use, reproduce, display, distribute, execute, and transmit the Software, and to prepare "
              "derivative works of the Software, and to permit third-parties to whom the Software is "
              "furnished to do so, all subject to the following:\\par "
              "The copyright notices in the Software and this entire statement, including the above "
              "license grant, this restriction and the following disclaimer, must be included in all "
              "copies of the Software, in whole or in part, and all derivative works of the Software, "
              "unless such copies or derivative works are solely in the form of machine-executable "
              "object code generated by a source language processor.\\par "
              LT_WARN("THE SOFTWARE IS PROVIDED \\\"AS IS\\\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
                      "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS "
                      "FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL THE "
                      "COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES "
                      "OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF "
                      "OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.")
              "\\par "
              LT_CREDIT
            },

            // ── 13: EUPL 1.2 ─────────────────────────────────────────────────
            { 13, "European Union Public Licence 1.2", "EUPL-1.2", "Logo_EUPL.svg.png",
              LT_HEAD
              LT_TITLE("European Union Public Licence")
              LT_SUB("Version 1.2  |  SPDX: EUPL-1.2")
              LT_BODY "EUPL \\u169? The European Union 2007, 2016\\par "
              "This European Union Public Licence (the \\\"EUPL\\\") applies to the Work (as defined "
              "below) which is provided under the terms of this Licence. Any use of the Work, other "
              "than as authorised under this Licence is prohibited.\\par "
              "The Work is provided under the terms of this Licence when the Licensor (as defined "
              "below) has placed the following notice immediately following the copyright notice for the Work:\\par "
              "\\pard\\qc\\sb60\\sa60\\cf1\\fs18 Licensed under the EUPL\\par "
              LT_BODY "{\\b 1. Definitions.} \\\"The Licence\\\": the present Licence. "
              "\\\"The Original Work\\\": the work or software distributed or communicated by the "
              "Licensor under this Licence. \\\"Derivative Works\\\": the works or software that "
              "could be created by the Licensee, based upon the Original Work or modifications thereof.\\par "
              "{\\b 2. Scope of the rights granted by the Licence.} The Licensor hereby grants You a "
              "world-wide, royalty-free, non-exclusive, sublicensable licence to do the following, "
              "for the duration of copyright vested in the Original Work: use the Work in any "
              "circumstance and for all usage; reproduce the Work; modify the Original Work, and make "
              "Derivative Works based upon the Work.\\par "
              LT_WARN("The Licensor provides the Work as-is and makes no representations or warranties "
                      "of any kind concerning the Work, express, implied, statutory or otherwise, including, "
                      "without limitation, warranties of title, merchantability, fitness for a particular "
                      "purpose, non-infringement, or the absence of latent or other defects, accuracy, or "
                      "the presence of absence of errors, whether or not discoverable.")
              "\\par "
              LT_CREDIT
            },

            // ── 14: CC0 1.0 ──────────────────────────────────────────────────
            { 14, "Creative Commons Zero v1.0 Universal", "CC0-1.0", "CC0-1.0.png",
              LT_HEAD
              LT_TITLE("Creative Commons Zero")
              LT_SUB("v1.0 Universal  |  SPDX: CC0-1.0")
              LT_BODY "<<AppName>>\\par "
              "The person who associated a work with this deed has dedicated the work to the public "
              "domain by waiving all of their rights to the work worldwide under copyright law, "
              "including all related and neighbouring rights, to the extent allowed by law.\\par "
              "You can copy, modify, distribute and perform the work, even for commercial purposes, "
              "all without asking permission. See Other Information below.\\par "
              "{\\b Other Information}\\par "
              "In no way are the patent or trademark rights of any person affected by CC0, nor are "
              "the rights that other persons may have in the work or in how the work is used, such "
              "as publicity or privacy rights.\\par "
              "Unless expressly stated otherwise, the person who associated a work with this deed "
              "makes no warranties about the work, and disclaims liability for all uses of the work, "
              "to the fullest extent permitted by applicable law.\\par "
              "When using or citing the work, you should not imply endorsement by the author or the "
              "affirmer.\\par "
              LT_CREDIT
            },

            // ── 15: CC-BY 4.0 ─────────────────────────────────────────────────
            { 15, "Creative Commons Attribution 4.0", "CC-BY-4.0", "CC-BY-4.0.png",
              LT_HEAD
              LT_TITLE("Creative Commons Attribution 4.0")
              LT_SUB("International  |  SPDX: CC-BY-4.0")
              LT_BODY "Copyright (c) <<Year>> <<AppName>>\\par "
              "This work is licensed under the Creative Commons Attribution 4.0 International License. "
              "To view a copy of this license, visit http://creativecommons.org/licenses/by/4.0/ "
              "or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.\\par "
              "{\\b You are free to:}\\par "
              "Share — copy and redistribute the material in any medium or format.\\par "
              "Adapt — remix, transform, and build upon the material for any purpose, even commercially.\\par "
              "{\\b Under the following terms:}\\par "
              "Attribution — You must give appropriate credit, provide a link to the license, and "
              "indicate if changes were made. You may do so in any reasonable manner, but not in any "
              "way that suggests the licensor endorses you or your use.\\par "
              "No additional restrictions — You may not apply legal terms or technological measures "
              "that legally restrict others from doing anything the license permits.\\par "
              LT_CREDIT
            },

            // ── 16: CC-BY-SA 4.0 ──────────────────────────────────────────────
            { 16, "Creative Commons Attribution-ShareAlike 4.0", "CC-BY-SA-4.0", "CC-BY-SA-4.0.png",
              LT_HEAD
              LT_TITLE("Creative Commons Attribution-ShareAlike 4.0")
              LT_SUB("International  |  SPDX: CC-BY-SA-4.0")
              LT_BODY "Copyright (c) <<Year>> <<AppName>>\\par "
              "This work is licensed under the Creative Commons Attribution-ShareAlike 4.0 International "
              "License. To view a copy of this license, visit http://creativecommons.org/licenses/by-sa/4.0/ "
              "or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.\\par "
              "{\\b You are free to:}\\par "
              "Share — copy and redistribute the material in any medium or format.\\par "
              "Adapt — remix, transform, and build upon the material for any purpose, even commercially.\\par "
              "{\\b Under the following terms:}\\par "
              "Attribution — You must give appropriate credit, provide a link to the license, and "
              "indicate if changes were made.\\par "
              "ShareAlike — If you remix, transform, or build upon the material, you must distribute "
              "your contributions under the same license as the original.\\par "
              "No additional restrictions — You may not apply legal terms or technological measures "
              "that legally restrict others from doing anything the license permits.\\par "
              LT_CREDIT
            },

            // ── 17: Artistic 2.0 ──────────────────────────────────────────────
            { 17, "Artistic License 2.0", "Artistic-2.0", "OpeenSourceInitiative.png",
              LT_HEAD
              LT_TITLE("Artistic License")
              LT_SUB("Version 2.0  |  SPDX: Artistic-2.0")
              LT_BODY "Copyright (c) <<Year>>, <<AppName>>\\par "
              "The intent of this document is to state the conditions under which a Package may be "
              "copied, such that the Copyright Holder maintains some semblance of artistic control "
              "over the development of the package, while giving the users of the package the right "
              "to use and distribute the Package in a more-or-less customary fashion, plus the right "
              "to make reasonable modifications.\\par "
              "{\\b Definitions.} \\\"The Package\\\" refers to the collection of files distributed "
              "by the Copyright Holder. \\\"Standard Version\\\" refers to such a Package if it has "
              "not been modified, or has been modified only in ways explicitly requested by the "
              "Copyright Holder.\\par "
              "{\\b 1.} You may make and give away verbatim copies of the source form of the Standard "
              "Version of this Package without restriction, provided that you duplicate all of the "
              "original copyright notices and associated disclaimers.\\par "
              "{\\b 2.} You may apply bug fixes, portability fixes and other modifications derived "
              "from the Public Domain or from the Copyright Holder. A Package modified in such a way "
              "shall still be considered the Standard Version.\\par "
              LT_WARN("THIS PACKAGE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS \\\"AS IS\\\" "
                      "AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES.")
              "\\par "
              LT_CREDIT
            },

            // ── 18: WTFPL ─────────────────────────────────────────────────────
            { 18, "Do What The F*ck You Want To Public License", "WTFPL", "wtfpl-badge-1.png",
              LT_HEAD
              LT_TITLE("Do What The F*ck You Want To")
              LT_SUB("Public License — Version 2  |  SPDX: WTFPL")
              LT_BODY "Copyright (C) <<Year>> <<AppName>>\\par "
              "Everyone is permitted to copy and distribute verbatim or modified copies of this "
              "license document, and changing it is allowed as long as the name is changed.\\par "
              "{\\b DO WHAT THE F*CK YOU WANT TO PUBLIC LICENSE}\\par "
              "TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION\\par "
              "{\\b 0.} You just DO WHAT THE F*CK YOU WANT TO.\\par "
              LT_CREDIT
            },
        };
        #undef LT_COLORTBL
        #undef LT_HEAD
        #undef LT_TITLE
        #undef LT_SUB
        #undef LT_BODY
        #undef LT_WARN
        #undef LT_CREDIT

        const int kLTCount = (int)(sizeof(kLT) / sizeof(kLT[0]));
        const char* seedLTSql =
            "INSERT OR IGNORE INTO license_templates (id, name, spdx_id, img_file, content_rtf) "
            "VALUES (?,?,?,?,?);";
        for (int i = 0; i < kLTCount; i++) {
            void* s2 = NULL;
            if (p_prepare(db, seedLTSql, -1, &s2, NULL) == 0) {
                std::string sId   = std::to_string(kLT[i].id);
                p_bind_text(s2, 1, sId.c_str(),       -1, NULL);
                p_bind_text(s2, 2, kLT[i].name,       -1, NULL);
                p_bind_text(s2, 3, kLT[i].spdx,       -1, NULL);
                p_bind_text(s2, 4, kLT[i].img,        -1, NULL);
                p_bind_text(s2, 5, kLT[i].rtf,        -1, NULL);
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
