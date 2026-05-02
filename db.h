#pragma once
#include <string>
#include <vector>
#include "deps.h"   // ExternalDep, DepDelivery, DepArch, DepOffline

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
    int use_components = 0;  // 0 = full package, 1 = component-based
    std::wstring app_id;     // Inno AppId GUID, e.g. "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"; auto-generated on creation
};

struct RegistryEntryRow {
    int id = 0;
    int project_id = 0;
    std::wstring hive;
    std::wstring path;
    std::wstring name;
    std::wstring type;
    std::wstring data;
    std::wstring flags;  // space-separated Inno [Registry] flags, e.g. "uninsdeletevalue 64bit"
};

struct ComponentRow {
    int id = 0;
    int project_id = 0;
    std::wstring display_name;
    std::wstring description;
    std::wstring notes_rtf;         // RTF-encoded rich notes (stored in DB, shown as installer tooltip)
    int is_required    = 0;        // 1 = always installed, 0 = optional
    int is_preselected = 0;        // 1 = ticked by default at install (implied when is_required==1)
    std::wstring source_type;      // L"folder" or L"file"
    std::wstring source_path;
    std::wstring dest_path;
    std::vector<int> dependencies; // in-memory dep IDs; persisted on Save
};

struct FileRow {
    int id = 0;
    int project_id = 0;
    std::wstring source_path;         // real disk path, or "" for virtual folders
    std::wstring destination_path;    // virtual tree path, e.g. "Program Files\MyApp\sub"
    std::wstring install_scope;       // "" | "AskAtInstall" | "__folder__"
    std::wstring inno_flags;          // space-separated Inno [Files] flags, e.g. "ignoreversion 32bit"
    std::wstring dest_dir_override;   // Inno dir constant override, e.g. "{sys}"; empty = use tree node default
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
    bool InsertFile(int projectId, const std::wstring &sourcePath, const std::wstring &destPath, const std::wstring &installScope, const std::wstring &innoFlags = L"", const std::wstring &destDirOverride = L"");
    std::vector<FileRow> GetFilesForProject(int projectId);
    // Registry entry persistence (relational: one project → many entries)
    bool InsertRegistryEntry(int projectId, const std::wstring &hive, const std::wstring &path, const std::wstring &name, const std::wstring &type, const std::wstring &data, const std::wstring &flags = L"");
    std::vector<RegistryEntryRow> GetRegistryEntriesForProject(int projectId);
    bool DeleteRegistryEntriesForProject(int projectId);
    // Component persistence
    int  InsertComponent(const ComponentRow &comp);
    bool UpdateComponent(const ComponentRow &comp);
    std::vector<ComponentRow> GetComponentsForProject(int projectId);
    bool DeleteComponentsForProject(int projectId);
    bool DeleteComponent(int id);
    // Component dependency persistence
    bool InsertComponentDependency(int componentId, int dependsOnId);
    std::vector<int> GetDependenciesForComponent(int componentId);
    bool DeleteDependenciesForComponent(int componentId);
    // Shortcut menu-node persistence
    struct ScMenuNodeRow {
        int id = 0;          // same id as ScMenuNode::id (0=SM root, 1=Programs, 2+= user)
        int project_id = 0;
        int parent_id = -1;
        std::wstring name;
    };
    bool InsertScMenuNode(int projectId, const ScMenuNodeRow& node);
    bool DeleteScMenuNodesForProject(int projectId);
    std::vector<ScMenuNodeRow> GetScMenuNodesForProject(int projectId);
    // Shortcut definition persistence (Desktop + Start Menu + pins)
    struct ScShortcutRow {
        int id = 0;          // same id as ShortcutDef::id
        int project_id = 0;
        int type = 0;        // SCT_* constant
        int sm_node_id = -1;
        std::wstring name;
        std::wstring exe_path;
        std::wstring working_dir;
        std::wstring icon_path;
        int icon_index = 0;
        int run_as_admin = 0;
        int pin_to_start = 0;
        int pin_to_taskbar = 0;
    };
    bool InsertScShortcut(int projectId, const ScShortcutRow& sc);
    bool DeleteScShortcutsForProject(int projectId);
    std::vector<ScShortcutRow> GetScShortcutsForProject(int projectId);
    // External dependency persistence
    int  InsertExternalDep(int projectId, const ExternalDep& dep);
    bool DeleteExternalDepsForProject(int projectId);
    std::vector<ExternalDep> GetExternalDepsForProject(int projectId);
    // Installer dialog content persistence
    bool UpsertInstallerDialog(int projectId, int dialogType, const std::wstring& rtf);
    bool DeleteInstallerDialogsForProject(int projectId);
    // Returns vector of (dialog_type, content_rtf) pairs ordered by dialog_type.
    std::vector<std::pair<int,std::wstring>> GetInstallerDialogsForProject(int projectId);
    // Returns all (dialog_type, content_rtf) default templates from dialog_defaults table.
    std::vector<std::pair<int,std::wstring>> GetAllDialogDefaults();
    // Script persistence
    struct ScriptRow {
        int          id                  = 0;
        int          project_id          = 0;
        std::wstring name;
        int          type                = 1;   // 0=bat 1=ps1
        std::wstring content;
        int          when_to_run         = 1;   // ScrWhenToRun integer
        int          run_hidden          = 0;
        int          wait_for_completion = 1;
        std::wstring description;               // Finish-page opt-out checkbox label
        int          also_uninstall      = 0;
        int          sort_order          = 0;
    };
    int  InsertScript(int projectId, const ScriptRow& s);
    bool DeleteScriptsForProject(int projectId);
    std::vector<ScriptRow> GetScriptsForProject(int projectId);
}
