-- SetupCraft SQLite schema
-- Creates the `projects` table used to track open projects.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    directory TEXT NOT NULL,
    last_updated INTEGER -- Unix epoch seconds
);

-- You can insert the initial project like this (example):
-- INSERT INTO projects (name, directory, last_updated) VALUES ('SetupCraft', 'C:/path/to/project', strftime('%s','now'));
