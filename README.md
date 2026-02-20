Skeleton: Minimal app template for new projects

Contents:
- `CMakeLists.txt` - minimal project file for a small Win32 app
- `makeit.bat` - helper to configure+build with CMake (MinGW default)
- `GPLv2.md` - license text (GPLv2)
- `.gitignore` - empty placeholder
- `wps_logo.svg` - simple SVG logo to use and convert for icons
- `sqlite3/` - placeholder for sqlite headers (see README inside)
- `mingw_dlls/` - place runtime DLLs here

How to use:
1. Open a terminal in this `Skeleton` folder.
2. Run `makeit.bat` (optionally pass generator and config).
3. Build artifacts are placed in `build/`.

Replace `main.cpp` with your application code or modify it as a starting point.
