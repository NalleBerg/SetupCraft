# _follow.ps1 — live-tail makeit.log for SetupCraft builds.
#
# Run this in a separate terminal before (or during) makeit.bat.
# Each time makeit.bat starts it truncates makeit.log; this script
# detects that, prints a RUN banner, and streams every line with colour
# and phase annotations.  The run counter is a per-session counter:
# it resets to 0 whenever you start _follow.ps1 and increments once
# per build.  Ctrl+C to stop.

$log       = Join-Path $PSScriptRoot 'makeit.log'
$countFile = Join-Path $PSScriptRoot 'makeit_count.txt'
$pos       = 0

# Reset counter to 0 for this session — first build will show as Run #1.
Set-Content $countFile 0
$run = 0

# ── Phase tracking ────────────────────────────────────────────────────────────
$script:phase      = ''
$script:buildStart = $null

function Enter-Phase($id, $label) {
    if ($script:phase -ne $id) {
        $script:phase = $id
        Write-Host ''
        Write-Host "  ┌─── $label" -ForegroundColor Magenta
        Write-Host ''
    }
}

# ── Progress bar ──────────────────────────────────────────────────────────────
# Returns a 38-character block bar for the given 0-100 percentage.
function Format-Bar($pct) {
    $w = 38
    $n = [int]([Math]::Round($pct / 100.0 * $w))
    ('█' * $n) + ('░' * ($w - $n))
}

# ── Per-line colour + annotation logic ───────────────────────────────────────
function Write-ColoredLine($line) {

    # ── Compiler diagnostics ─────────────────────────────────────────────────
    if ($line -match ':\s*error:') {
        Write-Host "  ✖  $line" -ForegroundColor Red;  return }
    if ($line -match ':\s*warning:') {
        Write-Host "  ⚠  $line" -ForegroundColor Yellow; return }
    if ($line -match ':\s*note:') {
        Write-Host "     $line" -ForegroundColor DarkCyan; return }

    # ── Build banner lines (printed by makeit.bat) ────────────────────────────
    if ($line -match '^={4,}') {
        Write-Host $line -ForegroundColor DarkCyan; return }
    if ($line -match 'SetupCraft Build') {
        Write-Host $line -ForegroundColor Cyan; return }
    if ($line -match '^\s+(Generator|Config)\s*:') {
        Write-Host $line -ForegroundColor DarkCyan; return }

    # ── [PRE] — kill running process ─────────────────────────────────────────
    if ($line -match '^\[PRE\]') {
        Enter-Phase 'pre' 'Pre-build  (stopping any running instance)'
        Write-Host "  $line" -ForegroundColor DarkGray; return }
    if ($line -match 'taskkill|SUCCESS.*SetupCraft\.exe|not found') {
        Write-Host "  $line" -ForegroundColor DarkGray; return }

    # ── [CONFIGURE] — cmake -S . -B build ────────────────────────────────────
    if ($line -match '^\[CONFIGURE\]') {
        Enter-Phase 'cfg' 'CMake — Configure  (generating Makefiles)'
        Write-Host "  $line" -ForegroundColor Cyan; return }
    if ($line -match '^-- ') {
        Write-Host "  $line" -ForegroundColor DarkCyan; return }
    if ($line -match '-- Configuring done|-- Generating done|-- Build files') {
        Write-Host "  ✔  $line" -ForegroundColor Green; return }

    # ── [BUILD] label from makeit.bat ────────────────────────────────────────
    if ($line -match '^\[BUILD\]') {
        Write-Host "  $line" -ForegroundColor Cyan; return }

    # ── [ X%] Building CXX object — show progress bar ────────────────────────
    if ($line -match '^\[\s*(\d+)%\]\s+Building CXX object.*[/\\]([^/\\]+\.cpp)\.obj') {
        Enter-Phase 'compile' 'Compiler  (object files)'
        $pct  = [int]$Matches[1]
        $file = $Matches[2]
        $bar  = Format-Bar $pct
        Write-Host ("  [{0}] {1,3}%  » {2}" -f $bar, $pct, $file) -ForegroundColor White
        return }

    # ── [ X%] Linking ─────────────────────────────────────────────────────────
    if ($line -match '^\[\s*(\d+)%\]\s+Linking') {
        Enter-Phase 'link' 'Linker  (producing SetupCraft.exe)'
        $pct = [int]$Matches[1]
        $bar = Format-Bar $pct
        Write-Host ("  [{0}] {1,3}%  » Linking CXX executable" -f $bar, $pct) -ForegroundColor Cyan
        return }

    # ── Other [ X%] cmake steps ───────────────────────────────────────────────
    if ($line -match '^\[\s*(\d+)%\]') {
        $pct  = [int]$Matches[1]
        $bar  = Format-Bar $pct
        $rest = $line -replace '^\[\s*\d+%\]\s*', ''
        Write-Host ("  [{0}] {1,3}%  {2}" -f $bar, $pct, $rest) -ForegroundColor Gray
        return }

    # ── Verbose compiler command lines (full g++ invocation) ─────────────────
    if ($line -match 'g\+\+\.exe|gcc\.exe|mingw.*\\ld') {
        Write-Host "     $line" -ForegroundColor DarkGray; return }
    if ($line -match '^\s{2,}-[A-Za-z]') {   # continuation flag lines
        Write-Host "     $line" -ForegroundColor DarkGray; return }

    # ── Build outcome ─────────────────────────────────────────────────────────
    if ($line -match 'Built target') {
        Write-Host "  ✔  $line" -ForegroundColor Green; return }
    if ($line -match '^\[BUILD\].*[Cc]omplete|^Build complete') {
        Write-Host "  ✔  $line" -ForegroundColor Green; return }
    if ($line -match '\[ERROR\]|FAILED|errorlevel') {
        Write-Host "  ✖  $line" -ForegroundColor Red; return }

    # ── [COPY-BUILD] — runtime files copied to build\ ─────────────────────────
    if ($line -match '^\[COPY-BUILD\]') {
        Enter-Phase 'cpbuild' 'Copy  (runtime assets → build\)'
        Write-Host "  $line" -ForegroundColor DarkCyan; return }

    # ── [PACKAGE] — assembling the SetupCraft\ output folder ─────────────────
    if ($line -match '^\[PACKAGE\]|^Copying executable|^Copying DLL') {
        Enter-Phase 'pkg' 'Package  (assembling SetupCraft\ output folder)'
        Write-Host "  $line" -ForegroundColor Cyan; return }
    if ($line -match '\d+\s+[Ff]ile') {
        Write-Host "  ✔  $line" -ForegroundColor Green; return }

    # ── [DONE] ────────────────────────────────────────────────────────────────
    if ($line -match '^Package created at') {
        Enter-Phase 'done' 'Done'
        Write-Host "  ✔  $line" -ForegroundColor Green; return }
    if ($line -match '^SetupCraft\.exe:') {
        Write-Host ("  ✔  Timestamp: " + ($line -replace '^SetupCraft\.exe:\s*', '')) `
            -ForegroundColor Green; return }
    if ($line -match '^\[DONE\]') {
        Write-Host "  ✔  $line" -ForegroundColor Green
        if ($script:buildStart) {
            $elapsed = [DateTime]::Now - $script:buildStart
            Write-Host ("  ⏱  Build time: {0}" -f $elapsed.ToString('mm\:ss\.f')) -ForegroundColor Cyan
        }
        return }

    # ── Fallback ──────────────────────────────────────────────────────────────
    Write-Host "  $line"
}

# ── Startup banner ────────────────────────────────────────────────────────────
Write-Host ''
Write-Host ('═' * 62) -ForegroundColor DarkCyan
Write-Host '  _follow.ps1  —  SetupCraft build watcher' -ForegroundColor Cyan
Write-Host "  Log : $log" -ForegroundColor DarkCyan
Write-Host '  Run counter reset to 0.  Ctrl+C to stop.' -ForegroundColor DarkCyan
Write-Host ('═' * 62) -ForegroundColor DarkCyan
Write-Host ''
Write-Host '  Waiting for makeit.bat to start...' -ForegroundColor DarkGray
Write-Host ''

# ── Main poll loop ────────────────────────────────────────────────────────────
while ($true) {
    if (Test-Path $log) {
        $info = Get-Item $log
        $size = $info.Length

        # Detect new run: log was truncated (makeit.bat started fresh).
        # Also fires on very first build when run=0 and content appears.
        if ($size -lt $pos -or ($run -eq 0 -and $size -gt 0)) {
            $run++
            Set-Content $countFile $run
            $pos          = 0
            $script:phase      = ''     # reset phase tracker for each run
            $script:buildStart = [DateTime]::Now
            Clear-Host
            Write-Host ''
            Write-Host ('═' * 62) -ForegroundColor Cyan
            Write-Host ("  BUILD RUN #{0}   {1}" -f $run,
                $info.LastWriteTime.ToString('yyyy-MM-dd  HH:mm:ss')) -ForegroundColor Cyan
            Write-Host '  SetupCraft  —  MinGW / CMake' -ForegroundColor Cyan
            Write-Host ('═' * 62) -ForegroundColor Cyan
            Write-Host ''
        }

        if ($size -gt $pos) {
            try {
                $fs = [IO.File]::Open($log,
                    [IO.FileMode]::Open,
                    [IO.FileAccess]::Read,
                    [IO.FileShare]::ReadWrite)
                $fs.Position = $pos
                $sr   = [IO.StreamReader]::new($fs)
                $text = $sr.ReadToEnd()
                $sr.Close(); $fs.Close()
                if ($text) {
                    foreach ($line in ($text -split "`r?`n")) {
                        if ($line -ne '') { Write-ColoredLine $line }
                    }
                    $pos = $size
                }
            } catch { }
        }
    }
    Start-Sleep -Milliseconds 150
}
