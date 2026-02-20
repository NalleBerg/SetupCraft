Place any required MinGW runtime DLLs here for packaging and testing.

Common files you may need to include when distributing an MinGW-built EXE:
- libgcc_s_seh-1.dll (or libgcc_s_dw2-1.dll depending on toolchain)
- libstdc++-6.dll (if using libstdc++)
- libwinpthread-1.dll
- sqlite3.dll (if using SQLite)

When creating a distribution package, copy selected DLLs from this folder into the final package along with the executable.
