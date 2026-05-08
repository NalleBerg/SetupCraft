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
    std::wstring flags;      // space-separated Inno [Registry] flags, e.g. "uninsdeletevalue 64bit"
    std::wstring components;  // Inno Components: field, e.g. "main extra"
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

struct FileAssocRow {
    int id = 0;
    int project_id = 0;
    int enabled = 1;               // row-level checkbox on the FA page
    std::wstring extension;        // e.g. ".myext" (with dot)
    std::wstring description;      // "MyApp Document"
    std::wstring prog_id;          // e.g. "MyApp.myext"; empty = auto-derive from AppName + extension
    std::wstring icon_path;        // e.g. "{app}\App.exe" or absolute disk path
    int icon_index = 0;
    std::wstring open_cmd;         // e.g. "\"{app}\App.exe\" \"%1\""
    std::wstring edit_cmd;         // optional
    std::wstring print_cmd;        // optional
    std::wstring content_type;     // MIME, e.g. "application/x-myext"
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
    bool InsertRegistryEntry(int projectId, const std::wstring &hive, const std::wstring &path, const std::wstring &name, const std::wstring &type, const std::wstring &data, const std::wstring &flags = L"", const std::wstring &components = L"");
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
        std::wstring arguments;
        std::wstring comment;
        std::wstring hotkey;
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
    // License template helpers.
    // GetAllLicenseTemplates: returns (id, display_name) pairs ordered by id.
    // GetLicenseTemplateRtf:  returns the RTF content for a given template id,
    //                         or an empty string if the id is not found.
    struct LicenseTemplateInfo { int id; std::wstring name; std::wstring img_file; };
    std::vector<LicenseTemplateInfo> GetAllLicenseTemplates();
    std::wstring GetLicenseTemplateRtf(int id);
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
        int          on_error            = 0;   // 0=continue (fail silently)  1=abort installation
        std::wstring working_dir;               // VFS path run in (e.g. "{app}\tools"); empty = default
        std::wstring parameters;                // extra command-line parameters passed to the script process; empty = none
        int          finish_checked_by_default = 0; // 1 = checkedonce flag: Finish-page opt-out checkbox starts checked
        std::wstring required_components;           // space-separated component display names; empty = always run
        int          run_elevated                = 0; // 1 = runascurrentuser (force elevated); 0 = default
        std::wstring notes;                             // developer annotation shown as tile subtitle; empty = none
    };
    int  InsertScript(int projectId, const ScriptRow& s);
    bool DeleteScriptsForProject(int projectId);
    std::vector<ScriptRow> GetScriptsForProject(int projectId);
    // File association persistence
    int  InsertFileAssoc(int projectId, const FileAssocRow& row);
    bool DeleteFileAssocsForProject(int projectId);
    std::vector<FileAssocRow> GetFileAssocsForProject(int projectId);
}
