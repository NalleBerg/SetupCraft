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
};

namespace DB {
    bool InitDb();
    bool InsertProject(const std::wstring &name, const std::wstring &directory, const std::wstring &description, const std::wstring &lang, const std::wstring &version, int &outId);
    std::vector<ProjectRow> ListProjects();
    bool GetProject(int id, ProjectRow &outProject);
    bool GetSetting(const std::wstring &key, std::wstring &outValue);
    bool SetSetting(const std::wstring &key, const std::wstring &value);
}
