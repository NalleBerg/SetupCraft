#include "db.h"
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <cstdio>

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
    const char *createProjects = "CREATE TABLE IF NOT EXISTS projects (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, directory TEXT NOT NULL, description TEXT, lang TEXT, version TEXT, created INTEGER, last_updated INTEGER, register_in_windows INTEGER DEFAULT 1, app_icon_path TEXT, app_publisher TEXT);";
    const char *createSettings = "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT);";
    const char *createRegistry = "CREATE TABLE IF NOT EXISTS registry_entries (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL, hive TEXT NOT NULL, path TEXT NOT NULL, name TEXT NOT NULL, type TEXT NOT NULL, data TEXT, FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);";
    const char *createFiles = "CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL, source_path TEXT NOT NULL, destination_path TEXT NOT NULL, FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE);";
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
    
    // Migrate existing projects table to add new columns if they don't exist
    p_exec(db, "ALTER TABLE projects ADD COLUMN register_in_windows INTEGER DEFAULT 1;", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE projects ADD COLUMN app_icon_path TEXT;", NULL, NULL, &errmsg);
    p_exec(db, "ALTER TABLE projects ADD COLUMN app_publisher TEXT;", NULL, NULL, &errmsg);
    // Add install_scope column to files table to store per-file or per-folder install scope (PerUser/AllUsers/AskAtInstall)
    p_exec(db, "ALTER TABLE files ADD COLUMN install_scope TEXT DEFAULT '';", NULL, NULL, &errmsg);
    
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

    const char *sql = "INSERT INTO projects (name, directory, description, lang, version, created, last_updated) VALUES (?, ?, ?, ?, ?, ?, ?);";
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
    if (p_bind_text) p_bind_text(stmt, 6, sEpoch.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 7, sEpoch.c_str(), -1, NULL);

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

bool DB::InsertFile(int projectId, const std::wstring &sourcePath, const std::wstring &destPath, const std::wstring &installScope) {
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return false;

    const char *sql = "INSERT INTO files (project_id, source_path, destination_path, install_scope) VALUES (?, ?, ?, ?);";
    void *stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != 0) { p_close(db); return false; }

    std::string sProject = std::to_string(projectId);
    std::string sSource = WToUtf8(sourcePath);
    std::string sDest = WToUtf8(destPath);
    std::string sScope = WToUtf8(installScope);

    if (p_bind_text) p_bind_text(stmt, 1, sProject.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 2, sSource.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 3, sDest.c_str(), -1, NULL);
    if (p_bind_text) p_bind_text(stmt, 4, sScope.c_str(), -1, NULL);

    p_step(stmt);
    if (p_finalize) p_finalize(stmt);
    p_close(db);
    return true;
}

std::vector<ProjectRow> DB::ListProjects() {
    std::vector<ProjectRow> out;
    std::wstring dbPath = GetAppDataDbPath();
    std::string dbPathUtf8 = WToUtf8(dbPath);
    void *db = NULL;
    int flags = 0x00000002 /*SQLITE_OPEN_READWRITE*/ | 0x00000004 /*SQLITE_OPEN_CREATE*/;
    if (p_open(dbPathUtf8.c_str(), &db, flags, NULL) != 0) return out;

    const char *sql = "SELECT id, name, directory, description, lang, version, created, last_updated FROM projects ORDER BY last_updated DESC;";
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
        r.name = Utf8ToW(t0 ? (const char*)t0 : "");
        r.directory = Utf8ToW(t1 ? (const char*)t1 : "");
        r.description = Utf8ToW(t2 ? (const char*)t2 : "");
        r.lang = Utf8ToW(t3 ? (const char*)t3 : "");
        r.version = Utf8ToW(t4 ? (const char*)t4 : "");
        r.created = t5 ? atoll((const char*)t5) : 0;
        r.last_updated = t6 ? atoll((const char*)t6) : 0;
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

    const char *sql = "SELECT id, name, directory, description, lang, version, created, last_updated FROM projects WHERE id = ?;";
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
        outProject.name = Utf8ToW(t0 ? (const char*)t0 : "");
        outProject.directory = Utf8ToW(t1 ? (const char*)t1 : "");
        outProject.description = Utf8ToW(t2 ? (const char*)t2 : "");
        outProject.lang = Utf8ToW(t3 ? (const char*)t3 : "");
        outProject.version = Utf8ToW(t4 ? (const char*)t4 : "");
        outProject.created = t5 ? atoll((const char*)t5) : 0;
        outProject.last_updated = t6 ? atoll((const char*)t6) : 0;
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
