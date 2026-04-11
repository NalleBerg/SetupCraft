# Extracts shell32.dll icon index 249 → multi-size .ico (16/32/48/256 px).
# Must run in 32-bit PowerShell so SysWOW64\shell32.dll PrivateExtractIconsW is available.
param([string]$OutPath)

# Re-launch as 32-bit PS if currently 64-bit
if ([IntPtr]::Size -eq 8) {
    $ps32 = "$env:SystemRoot\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
    & $ps32 -ExecutionPolicy Bypass -File $PSCommandPath -OutPath $OutPath
    exit $LASTEXITCODE
}

Add-Type -AssemblyName System.Drawing

$shell32 = Join-Path $env:SystemRoot "SysWOW64\shell32.dll"
if (-not (Test-Path $shell32)) { $shell32 = Join-Path $env:SystemRoot "System32\shell32.dll" }

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Drawing;
public class PE2 {
    [DllImport("shell32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
    public static extern uint PrivateExtractIconsW(
        string file, int index, int cx, int cy,
        [Out] IntPtr[] hicons, [Out] uint[] ids, uint n, uint flags);
    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr h);

    public static System.Drawing.Bitmap GetIconBitmap(string dll, int idx, int size) {
        IntPtr[] h = new IntPtr[1];
        uint[]   id = new uint[1];
        uint got = PrivateExtractIconsW(dll, idx, size, size, h, id, 1, 0);
        if (got == 0 || h[0] == IntPtr.Zero) return null;
        System.Drawing.Bitmap bmp;
        using (var ico = System.Drawing.Icon.FromHandle(h[0])) { bmp = ico.ToBitmap(); }
        DestroyIcon(h[0]);
        if (bmp.Width == size && bmp.Height == size) return bmp;
        var dst = new System.Drawing.Bitmap(size, size);
        using (var g = System.Drawing.Graphics.FromImage(dst)) {
            g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
            g.DrawImage(bmp, 0, 0, size, size);
        }
        bmp.Dispose();
        return dst;
    }
}
'@

function Write-Ico($bitmaps, $path) {
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $n  = $bitmaps.Count
    $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$n)

    $pngs = $bitmaps | ForEach-Object {
        $t = New-Object System.IO.MemoryStream
        $_.Save($t, [System.Drawing.Imaging.ImageFormat]::Png)
        $t.ToArray()
    }

    $offset = 6 + $n * 16
    foreach ($i in 0..($n-1)) {
        $sz  = $bitmaps[$i].Width
        $len = $pngs[$i].Length
        if ($sz -eq 256) { $bw.Write([byte]0) } else { $bw.Write([byte]$sz) }
        if ($sz -eq 256) { $bw.Write([byte]0) } else { $bw.Write([byte]$sz) }
        $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([uint16]1); $bw.Write([uint16]32)
        $bw.Write([uint32]$len); $bw.Write([uint32]$offset)
        $offset += $len
    }
    foreach ($png in $pngs) { $bw.Write($png) }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($path, $ms.ToArray())
    $bw.Dispose(); $ms.Dispose()
}

$bmps = @(16, 32, 48, 256) | ForEach-Object { [PE2]::GetIconBitmap($shell32, 249, $_) } | Where-Object { $_ -ne $null }
if ($bmps.Count -eq 0) { Write-Error "No icons extracted from $shell32"; exit 1 }
Write-Ico $bmps $OutPath
$bmps | ForEach-Object { $_.Dispose() }
Write-Host "Multi-size icon ($($bmps.Count) sizes) saved to $OutPath"
