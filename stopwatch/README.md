# Stopwatch

A lightweight, portable, always-on-top stopwatch for Windows.  
Designed for timing tasks — optionally launches another application at the same time as the clock starts.

---

## Quick Start

1. Run **stopwatch.exe**
2. The **Settings** screen appears:
   - Leave the path field blank to use it as a plain stopwatch
   - Or click **Browse…** to pick an `.exe` to launch alongside the clock
3. Click **Start Stopwatch**
4. The stopwatch window opens at the bottom-right of your screen

---

## Settings Screen

| Control | Description |
|---|---|
| Path field | Full path to an application to launch when the clock starts |
| **Browse…** | Opens a file picker to choose an `.exe` |
| **Start Stopwatch** | Saves the path, launches the app (if set), and starts the clock |

The last-used path is saved automatically and restored on the next launch.

---

## Stopwatch Window

```
┌─────────────────────────────┐
│  Reset                      │  ← big button, always accessible
│                             │
│    00:00:00:00              │  ← HH:MM:SS:cs  (centiseconds)
│                             │
│  Stop     │    Pause        │
│  Unpin from Top             │
└─────────────────────────────┘
```

### Buttons

| Button | What it does |
|---|---|
| **Reset** | Zeroes the elapsed time. The clock keeps running — it does **not** stop. |
| **Stop / Start** | Stops the clock (accumulates elapsed time). Press again to resume from where it stopped. Button is **red** while running, **green** while stopped. |
| **Pause / Resume** | Freezes the **display** only — the clock keeps ticking in the background. A yellow label *"DISPLAY PAUSED — clock running"* appears. Press **Resume** to jump the display back to the current live time. |
| **Unpin from Top / Pin to Top** | Toggles always-on-top. When pinned the button shows *Unpin from Top* (purple); when unpinned it shows *Pin to Top* (dark grey). |

### Display format

```
00:00:00:00
HH MM SS cs
```

- `HH` — hours  
- `MM` — minutes  
- `SS` — seconds  
- `cs` — centiseconds (1/100 s), updates every 10 ms

---

## Settings file (INI)

The app saves the last-used path to `stopwatch.ini`.

| Location | When used |
|---|---|
| Same folder as `stopwatch.exe` | If that folder is writable (portable USB drive, home folder, etc.) |
| `%APPDATA%\Stopwatch\stopwatch.ini` | Fallback when the exe folder is read-only (e.g. `Program Files`) |

---

## Portability

- **No installer** — just copy `stopwatch.exe` anywhere and run it
- **Statically linked** — no extra DLL files needed (no Visual C++ Redistributable, no MinGW DLLs)
- **Single file** — the entire app is one `.exe`

---

## Building from source

Requires MinGW-w64 and CMake.

```powershell
cd stopwatch
mkdir build; cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make
copy stopwatch.exe ..
```
