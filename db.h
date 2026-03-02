#pragma once
#include <string>
#include <vector>

struct ProjectRow {
    int id;
    std::wstring name;
    std::wstring directory;
    std::wstring description;
    std::wstring lang;
    std::wstring version;
    long long created;
    long long last_updated;
    int register_in_windows = 1;  // 1 = yes, 0 = no
    std::wstring app_icon_path;
    std::wstring app_publisher;
};

struct RegistryEntryRow {
    int id = 0;
    int project_id = 0;
    std::wstring hive;
    std::wstring path;
    std::wstring name;
    std::wstring type;
    std::wstring data;
};

namespace DB {
    bool InitDb();
    bool InsertProject(const std::wstring &name, const std::wstring &directory, const std::wstring &description, const std::wstring &lang, const std::wstring &version, int &outId);
    bool UpdateProject(const ProjectRow &project);
    std::vector<ProjectRow> ListProjects();
    bool GetProject(int id, ProjectRow &outProject);
    bool GetSetting(const std::wstring &key, std::wstring &outValue);
    bool SetSetting(const std::wstring &key, const std::wstring &value);
    // File persistence
    bool DeleteFilesForProject(int projectId);
    bool InsertFile(int projectId, const std::wstring &sourcePath, const std::wstring &destPath, const std::wstring &installScope);
    // Registry entry persistence (relational: one project → many entries)
    bool InsertRegistryEntry(int projectId, const std::wstring &hive, const std::wstring &path, const std::wstring &name, const std::wstring &type, const std::wstring &data);
    std::vector<RegistryEntryRow> GetRegistryEntriesForProject(int projectId);
    bool DeleteRegistryEntriesForProject(int projectId);
}
