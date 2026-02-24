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
    app_publisher TEXT
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

-- You can insert the initial project like this (example):
-- INSERT INTO projects (name, directory, last_updated) VALUES ('SetupCraft', 'C:/path/to/project', strftime('%s','now'));
