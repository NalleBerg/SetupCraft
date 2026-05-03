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
        "(id, project_id, type, sm_node_id, name, exe_path, working_dir, arguments, comment, icon_path, icon_index, run_as_admin, pin_to_start, pin_to_taskbar) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
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
    if (p_bind_text) p_bind_text(stmt, 10, sIcoP.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 11, sIcoI.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 12, sAdm.c_str(),  -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 13, sPinS.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 14, sPinT.c_str(), -1, NULL);
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
    const char *sql = "SELECT id, type, sm_node_id, name, exe_path, working_dir, arguments, comment, "
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
        r.name        = Utf8ToW(c3 ? (const char*)c3 : "");
        r.exe_path    = Utf8ToW(c4 ? (const char*)c4 : "");
        r.working_dir = Utf8ToW(c5 ? (const char*)c5 : "");
        r.arguments   = Utf8ToW(c6 ? (const char*)c6 : "");
        r.comment     = Utf8ToW(c7 ? (const char*)c7 : "");
        r.icon_path   = Utf8ToW(c8 ? (const char*)c8 : "");
        r.icon_index  = (int)p_col_int64(stmt, 9);
        r.run_as_admin = (int)p_col_int64(stmt, 10);
        r.pin_to_start   = (int)p_col_int64(stmt, 11);
        r.pin_to_taskbar = (int)p_col_int64(stmt, 12);
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
        "instructions, offline_behavior) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
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
    p_bind_text(stmt, 17, sOffline.c_str(),   -1, NULL);
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
        "credits_text, instructions, offline_behavior "
        "FROM external_deps WHERE project_id=? ORDER BY install_order ASC, id ASC;";
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
        d.offline_behavior = (DepOffline)(int)p_col_int64(stmt, 16);
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
