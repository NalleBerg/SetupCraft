-- SetupCraft SQLite schema
-- Creates the `projects` table used to track open projects.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    directory TEXT NOT NULL,
    description TEXT,
    lang TEXT,
    version TEXT,
    created INTEGER,
    last_updated INTEGER, -- Unix epoch seconds
    register_in_windows INTEGER DEFAULT 1,
    app_icon_path TEXT,
    app_publisher TEXT,
    use_components INTEGER DEFAULT 0  -- 0 = full package, 1 = component-based installation
);

CREATE TABLE IF NOT EXISTS registry_entries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    hive TEXT NOT NULL,
    path TEXT NOT NULL,
    name TEXT NOT NULL,
    type TEXT NOT NULL,
    data TEXT,
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    source_path TEXT NOT NULL,
    destination_path TEXT NOT NULL,
    install_scope TEXT DEFAULT '',
    inno_flags TEXT DEFAULT '',
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS components (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',   -- Developer-set name shown to end user
    description TEXT DEFAULT '',
    notes_rtf TEXT DEFAULT '',               -- RTF-encoded rich notes shown as installer tooltip
    is_required INTEGER DEFAULT 0,           -- 1 = always installed, 0 = optional
    is_preselected INTEGER DEFAULT 0,         -- 1 = ticked by default at install
    source_type TEXT DEFAULT 'folder',       -- 'folder' or 'file'
    source_path TEXT DEFAULT '',
    dest_path TEXT DEFAULT '',
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
);

-- You can insert the initial project like this (example):
-- INSERT INTO projects (name, directory, last_updated) VALUES ('SetupCraft', 'C:/path/to/project', strftime('%s','now'));
