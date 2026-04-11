# Extracts shell32.dll icon index 249 -> multi-size .ico (16/32/48/256 px).
# BMP-encoded ICO entries (required by windres/GNU resource compiler).
param([string]$OutPath)

Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Collections.Generic;
public class IcoMaker {
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern int ExtractIconExW(string file, int index,
        IntPtr[] phLarge, IntPtr[] phSmall, uint nIcons);
    [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr h);
    [StructLayout(LayoutKind.Sequential)]
    public struct ICONINFO { public bool fIcon; public int xh; public int yh; public IntPtr hbmMask; public IntPtr hbmColor; }
    [DllImport("user32.dll")] public static extern IntPtr CreateIconIndirect(ref ICONINFO ii);
    [DllImport("gdi32.dll")]  public static extern bool DeleteObject(IntPtr h);

    public static byte[] GetIcoBytes(string dll, int idx, int size) {
        IntPtr[] lg = new IntPtr[1];
        if (ExtractIconExW(dll, idx, lg, null, 1) <= 0 || lg[0] == IntPtr.Zero) return null;
        Bitmap bmp;
        using (var ico = Icon.FromHandle(lg[0])) { bmp = ico.ToBitmap(); }
        DestroyIcon(lg[0]);
        if (bmp.Width != size) {
            var dst = new Bitmap(size, size);
            using (var g = Graphics.FromImage(dst)) {
                g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                g.DrawImage(bmp, 0, 0, size, size);
            }
            bmp.Dispose(); bmp = dst;
        }
        IntPtr hBmp = bmp.GetHbitmap();
        bmp.Dispose();
        var ii = new ICONINFO { fIcon = true, hbmColor = hBmp, hbmMask = hBmp };
        IntPtr hIco = CreateIconIndirect(ref ii);
        DeleteObject(hBmp);
        if (hIco == IntPtr.Zero) return null;
        byte[] data;
        using (var ico2 = Icon.FromHandle(hIco))
        using (var ms   = new MemoryStream()) { ico2.Save(ms); data = ms.ToArray(); }
        DestroyIcon(hIco);
        return data;
    }

    public static byte[] MergeIcos(List<byte[]> icoList) {
        int n = icoList.Count;
        var imgData = new byte[n][];
        var entries = new byte[n][];
        for (int i = 0; i < n; i++) {
            byte[] ico = icoList[i];
            // Single-size ICO: 6 byte header + 16 byte entry = 22 bytes before data
            entries[i] = new byte[16];
            Array.Copy(ico, 6, entries[i], 0, 16);
            uint dataSize = BitConverter.ToUInt32(ico, 6 + 8);
            uint dataOff  = BitConverter.ToUInt32(ico, 6 + 12);
            imgData[i] = new byte[dataSize];
            Array.Copy(ico, (int)dataOff, imgData[i], 0, (int)dataSize);
        }
        using (var ms = new MemoryStream())
        using (var bw = new BinaryWriter(ms)) {
            bw.Write((ushort)0); bw.Write((ushort)1); bw.Write((ushort)n);
            uint off = (uint)(6 + n * 16);
            for (int i = 0; i < n; i++) {
                byte[] e = (byte[])entries[i].Clone();
                Array.Copy(BitConverter.GetBytes(off), 0, e, 12, 4);
                bw.Write(e);
                off += (uint)imgData[i].Length;
            }
            foreach (var d in imgData) bw.Write(d);
            return ms.ToArray();
        }
    }
}
"@

$shell32  = "$env:SystemRoot\System32\shell32.dll"
$icoList  = [System.Collections.Generic.List[byte[]]]::new()
foreach ($sz in @(16, 32, 48, 256)) {
    $b = [IcoMaker]::GetIcoBytes($shell32, 249, $sz)
    if ($b) { $icoList.Add($b) } else { Write-Warning "Could not extract $sz px" }
}

if ($icoList.Count -eq 0) { Write-Error "No icons extracted"; exit 1 }
$merged = [IcoMaker]::MergeIcos($icoList)
[System.IO.File]::WriteAllBytes($OutPath, $merged)
Write-Host "Multi-size BMP icon ($($icoList.Count) sizes) saved to $OutPath ($($merged.Length) bytes)"
