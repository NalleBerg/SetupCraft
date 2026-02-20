#pragma once
#include <string>
#include <vector>

struct ProjectRow {
    int id;
    std::wstring name;
    std::wstring directory;
    long long last_updated;
};

namespace DB {
    bool InitDb();
    bool InsertProject(const std::wstring &name, const std::wstring &directory, int &outId);
    std::vector<ProjectRow> ListProjects();
}
